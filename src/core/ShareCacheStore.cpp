#include "ShareCacheStore.h"

#include <algorithm>

#include "core/PathUtil.h"

namespace sharecache {

namespace {

bool nameLess(const Entry &a, const Entry &b)
{
    return a.file.name < b.file.name;
}

bool isAtOrUnder(const QString &path, const QString &root)
{
    if (root == QLatin1String("/")) {
        return true; // every share-absolute path is under the root
    }
    return path == root
           || (path.startsWith(root) && path.size() > root.size()
               && path.at(root.size()) == QLatin1Char('/'));
}

// Coalesces consecutive rows into ranges. Rows must arrive ascending.
void appendRow(QList<RowRange> &ranges, int row)
{
    if (!ranges.isEmpty() && ranges.last().first + ranges.last().count == row) {
        ++ranges.last().count;
    } else {
        ranges.append({row, 1});
    }
}

bool displayedFieldsDiffer(const FileEntry &a, const FileEntry &b)
{
    return a.isDir != b.isDir || a.size != b.size || a.modified != b.modified
           || a.inode != b.inode;
}

void appendUnique(QStringList &list, const QString &value)
{
    if (!list.contains(value)) {
        list.append(value);
    }
}

} // namespace

Store::Store(Clock clock)
    : m_clock(clock ? std::move(clock) : Clock([] { return std::chrono::steady_clock::now(); }))
{
}

// --- Private helpers --------------------------------------------------------

int Store::rowOf(const QList<Entry> &entries, const QString &name)
{
    const auto it = std::lower_bound(entries.cbegin(), entries.cend(), name,
                                     [](const Entry &e, const QString &n) { return e.file.name < n; });
    if (it == entries.cend() || it->file.name != name) {
        return -1;
    }
    return int(it - entries.cbegin());
}

bool Store::needsStat(const Entry &entry)
{
    return !entry.file.isDir
           && (entry.file.nlink == FileEntry::kNlinkUnknown || entry.nlinkStale);
}

Store::Record *Store::findRecord(const QString &key)
{
    const auto it = m_dirs.find(key);
    return it == m_dirs.end() ? nullptr : &it.value();
}

const Store::Record *Store::findRecord(const QString &key) const
{
    const auto it = m_dirs.constFind(key);
    return it == m_dirs.cend() ? nullptr : &it.value();
}

Entry *Store::findEntryMutable(const QString &path, QString *keyOut, int *rowOut)
{
    const QString p = pathutil::normalize(path);
    if (p == QLatin1String("/")) {
        return nullptr;
    }
    const QString key = pathutil::folderOf(p);
    Record *rec = findRecord(key);
    if (!rec) {
        return nullptr;
    }
    const int row = rowOf(rec->dir.entries, pathutil::nameOf(p));
    if (row < 0) {
        return nullptr;
    }
    if (keyOut) {
        *keyOut = key;
    }
    if (rowOut) {
        *rowOut = row;
    }
    return &rec->dir.entries[row];
}

void Store::indexAdd(const QString &key, const Entry &entry)
{
    if (!entry.file.isDir && entry.file.inode != 0) {
        m_inodes.insert(entry.file.inode, pathutil::join(key, entry.file.name));
    }
}

void Store::indexRemove(const QString &key, const Entry &entry)
{
    if (!entry.file.isDir && entry.file.inode != 0) {
        m_inodes.remove(entry.file.inode, pathutil::join(key, entry.file.name));
    }
}

// A Stale mark on a Loading directory cannot know whether the reply in
// flight predates the mutation, so it is deferred to landing time instead.
void Store::markStale(const QString &key, Invalidation *out)
{
    if (Record *rec = findRecord(key)) {
        markStaleRecord(*rec, key, out);
    }
}

void Store::markStaleRecord(Record &rec, const QString &key, Invalidation *out)
{
    if (rec.dir.state == State::Loading) {
        rec.staleOnLand = true;
    } else {
        rec.dir.state = State::Stale;
    }
    rec.statCursor = 0;
    if (out) {
        appendUnique(out->staleDirectories, key);
    }
}

void Store::markStaleUnder(const QString &path, Invalidation *out)
{
    for (auto it = m_dirs.begin(); it != m_dirs.end(); ++it) {
        if (isAtOrUnder(it.key(), path)) {
            markStaleRecord(it.value(), it.key(), out);
        }
    }
}

void Store::staleLinkCountsOf(quint64 inode, Invalidation *out)
{
    if (inode == 0) {
        return;
    }
    const QList<QString> paths = m_inodes.values(inode);
    for (const QString &path : paths) {
        QString key;
        int row = -1;
        Entry *e = findEntryMutable(path, &key, &row);
        if (!e || e->file.nlink == FileEntry::kNlinkUnknown) {
            continue; // Unknown is already a stat candidate; not "stale"
        }
        e->nlinkStale = true;
        findRecord(key)->statCursor = 0;
        if (out) {
            out->staleLinkCounts.append({key, row});
        }
    }
}

// --- Reads ------------------------------------------------------------------

const Directory *Store::find(const QString &dir) const
{
    const Record *rec = findRecord(pathutil::normalize(dir));
    return rec ? &rec->dir : nullptr;
}

State Store::state(const QString &dir) const
{
    const Directory *d = find(dir);
    return d ? d->state : State::Missing;
}

const Entry *Store::findEntry(const QString &path) const
{
    return const_cast<Store *>(this)->findEntryMutable(path, nullptr, nullptr);
}

QStringList Store::pathsWithInode(quint64 inode) const
{
    return inode == 0 ? QStringList() : QStringList(m_inodes.values(inode));
}

int Store::directoryCount() const
{
    return int(m_dirs.size());
}

qsizetype Store::entryCount() const
{
    qsizetype n = 0;
    for (const Record &rec : m_dirs) {
        n += rec.dir.entries.size();
    }
    return n;
}

// --- Fetch lifecycle --------------------------------------------------------

void Store::markLoading(const QString &dir)
{
    Record &rec = m_dirs[pathutil::normalize(dir)];
    rec.dir.state = State::Loading;
}

Diff Store::applyListing(const QString &dir, const QList<FileEntry> &entries)
{
    const QString key = pathutil::normalize(dir);
    Record &rec = m_dirs[key];

    QList<Entry> incoming;
    incoming.reserve(entries.size());
    for (const FileEntry &fe : entries) {
        incoming.append(Entry{fe, {}, false});
    }
    std::stable_sort(incoming.begin(), incoming.end(), nameLess);
    // A duplicate name cannot come from a real share; keep the first so the
    // sorted-unique invariant holds unconditionally.
    incoming.erase(std::unique(incoming.begin(), incoming.end(),
                               [](const Entry &a, const Entry &b) {
                                   return a.file.name == b.file.name;
                               }),
                   incoming.end());

    Diff diff;
    QList<RowRange> removedAscending;
    const QList<Entry> &old = rec.dir.entries;
    qsizetype i = 0;
    qsizetype j = 0;
    while (i < old.size() || j < incoming.size()) {
        int cmp = 0;
        if (i == old.size()) {
            cmp = 1;
        } else if (j == incoming.size()) {
            cmp = -1;
        } else {
            cmp = QString::compare(old[i].file.name, incoming[j].file.name);
        }

        if (cmp < 0) {
            indexRemove(key, old[i]);
            appendRow(removedAscending, int(i));
            ++i;
        } else if (cmp > 0) {
            indexAdd(key, incoming[j]);
            appendRow(diff.inserted, int(j));
            ++j;
        } else {
            const Entry &o = old[i];
            Entry &n = incoming[j];
            if (o.file.inode == n.file.inode) {
                // Same file: the link count carries over, but a re-stat is
                // wanted since the re-list may have been prompted by a change.
                n.file.nlink = o.file.nlink;
                n.nlinkFetchedAt = o.nlinkFetchedAt;
                n.nlinkStale = o.file.nlink != FileEntry::kNlinkUnknown;
            } else {
                // Replaced under the same name: nothing about the old inode
                // applies.
                n.file.nlink = FileEntry::kNlinkUnknown;
                n.nlinkFetchedAt = {};
                n.nlinkStale = false;
            }
            if (displayedFieldsDiffer(o.file, n.file)) {
                appendRow(diff.changed, int(j));
                indexRemove(key, o);
                indexAdd(key, n);
            }
            ++i;
            ++j;
        }
    }
    std::reverse(removedAscending.begin(), removedAscending.end());
    diff.removed = std::move(removedAscending);

    rec.dir.entries = std::move(incoming);
    rec.dir.state = rec.staleOnLand ? State::Stale : State::Fresh;
    rec.dir.fetchedAt = m_clock();
    rec.dir.error.clear();
    rec.staleOnLand = false;
    rec.statCursor = 0;
    return diff;
}

void Store::applyListFailure(const QString &dir, const QString &message)
{
    Record &rec = m_dirs[pathutil::normalize(dir)];
    rec.dir.state = State::Failed;
    rec.dir.error = message;
    rec.staleOnLand = false;
}

StatResult Store::applyStat(const QString &path, int nlink, quint64 inode)
{
    StatResult result;
    Entry *e = findEntryMutable(path, &result.ref.dir, &result.ref.row);
    if (!e) {
        return result;
    }
    e->file.nlink = nlink;
    e->nlinkFetchedAt = m_clock();
    const bool mismatch = e->file.inode != 0 && inode != 0 && e->file.inode != inode;
    if (mismatch) {
        // The name now refers to another file. Keep the count so the row is
        // not re-requested ahead of Unknown ones, flag it, and let the
        // parent's re-list bring the new inode in.
        e->nlinkStale = true;
        markStale(result.ref.dir, nullptr);
        result.outcome = StatOutcome::InodeMismatch;
    } else {
        e->nlinkStale = false;
        result.outcome = StatOutcome::Applied;
    }
    return result;
}

StatResult Store::applyStatFailure(const QString &path)
{
    StatResult result;
    Entry *e = findEntryMutable(path, &result.ref.dir, &result.ref.row);
    if (!e) {
        return result;
    }
    e->file.nlink = FileEntry::kNlinkUnavailable;
    e->nlinkFetchedAt = m_clock();
    e->nlinkStale = false;
    result.outcome = StatOutcome::Applied;
    return result;
}

// --- Invalidation rules -----------------------------------------------------

Invalidation Store::noteRenamed(const QString &from, const QString &to)
{
    Invalidation inv;
    const QString f = pathutil::normalize(from);
    const QString t = pathutil::normalize(to);
    markStale(pathutil::folderOf(f), &inv);
    markStale(pathutil::folderOf(t), &inv);
    markStaleUnder(f, &inv);
    markStaleUnder(t, &inv);
    return inv;
}

Invalidation Store::noteLinked(const QString &existingPath, const QString &newLinkPath)
{
    Invalidation inv;
    const QString existing = pathutil::normalize(existingPath);
    const QString newLink = pathutil::normalize(newLinkPath);
    if (const Entry *e = findEntry(existing)) {
        staleLinkCountsOf(e->file.inode, &inv);
    }
    markStale(pathutil::folderOf(existing), &inv);
    markStale(pathutil::folderOf(newLink), &inv);
    return inv;
}

Invalidation Store::noteUnlinked(const QString &path)
{
    Invalidation inv;
    const QString p = pathutil::normalize(path);
    if (const Entry *e = findEntry(p)) {
        staleLinkCountsOf(e->file.inode, &inv);
    }
    markStale(pathutil::folderOf(p), &inv);
    markStaleUnder(p, &inv);
    return inv;
}

void Store::markAllStale()
{
    for (auto it = m_dirs.begin(); it != m_dirs.end(); ++it) {
        markStaleRecord(it.value(), it.key(), nullptr);
        for (Entry &e : it->dir.entries) {
            if (!e.file.isDir && e.file.nlink != FileEntry::kNlinkUnknown) {
                e.nlinkStale = true;
            }
        }
    }
}

// --- Stat ordering ----------------------------------------------------------

QStringList Store::nextStats(const QList<Subscription> &subscriptions,
                             const QSet<QString> &exclude, int max)
{
    QStringList out;
    QSet<QString> seen;
    if (max <= 0) {
        return out;
    }

    // Unknown counts go before merely stale ones within each group; the
    // groups are appended in order, truncated to what is still wanted.
    QStringList unknown;
    QStringList stale;
    const auto flush = [&] {
        for (const QString &p : unknown) {
            if (out.size() < max) {
                out.append(p);
            }
        }
        for (const QString &p : stale) {
            if (out.size() < max) {
                out.append(p);
            }
        }
        unknown.clear();
        stale.clear();
    };
    const auto consider = [&](const QString &key, const Entry &e) {
        const QString path = pathutil::join(key, e.file.name);
        if (exclude.contains(path) || seen.contains(path)) {
            return;
        }
        seen.insert(path);
        if (e.file.nlink == FileEntry::kNlinkUnknown) {
            unknown.append(path);
        } else {
            stale.append(path);
        }
    };

    for (const Subscription &sub : subscriptions) {
        if (out.size() >= max) {
            break;
        }
        const QString key = pathutil::normalize(sub.dir);
        Record *rec = findRecord(key);
        if (!rec || rec->dir.entries.isEmpty()) {
            continue;
        }
        const QList<Entry> &entries = rec->dir.entries;

        for (const QString &name : sub.priorityNames) {
            const int row = rowOf(entries, name);
            if (row >= 0 && needsStat(entries[row])) {
                consider(key, entries[row]);
            }
        }
        flush();
        if (out.size() >= max) {
            break;
        }

        // The cursor skips rows already resolved on earlier calls; it parks
        // on the first row still wanted (or excluded, i.e. in flight), so a
        // pump over a huge directory stays cheap once its head is resolved.
        const int remaining = max - int(out.size());
        int i = rec->statCursor;
        while (i < entries.size() && !needsStat(entries[i])) {
            ++i;
        }
        rec->statCursor = i;
        for (; i < entries.size() && unknown.size() + stale.size() < remaining; ++i) {
            if (needsStat(entries[i])) {
                consider(key, entries[i]);
            }
        }
        flush();
    }
    return out;
}

void Store::clear()
{
    m_dirs.clear();
    m_inodes.clear();
}

} // namespace sharecache
