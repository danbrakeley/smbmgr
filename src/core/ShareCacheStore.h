#pragma once

#include <QHash>
#include <QList>
#include <QMultiHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include <chrono>
#include <functional>

#include "smb/SmbTypes.h"

// The share cache's data layer: every directory listing the app has fetched,
// the lazily stat'ed link counts, and the bookkeeping that says which of it
// is still trustworthy. Pure logic: no QObject, no session, no timers. The
// clock is injected so tests drive every timestamp. Nothing here ever talks
// to the server; the QObject facade that owns the SmbSession asks the store
// what to fetch and feeds the replies back in.
//
// Directory keys are share-absolute paths run through pathutil::normalize.
// Entries are stored sorted by name (QString::compare, case-sensitive, never
// folded) and found by binary search, so a listing that merely comes back in
// a different order produces an empty diff.
//
// Not thread-safe: the store lives on the GUI thread with its facade, like
// everything else in the app. Pointers returned by the reads are valid only
// until the next non-const call; hold an EntryRef (dir, row) across calls
// instead, and re-resolve it after a listing lands.
namespace sharecache {

using TimePoint = std::chrono::steady_clock::time_point;
// Must not call back into the store: it runs while a record is mid-update.
using Clock = std::function<TimePoint()>;

enum class State {
    Missing, // never fetched (no record)
    Loading, // a listing is in flight; old entries, if any, are still served
    Fresh,   // listed and nothing has invalidated it since
    Stale,   // served, but a re-list is wanted
    Failed,  // the last listing failed; old entries, if any, are still served
};

struct Entry
{
    FileEntry file;                // file.nlink is the served link count
    TimePoint nlinkFetchedAt = {}; // meaningful when file.nlink != kNlinkUnknown
    bool nlinkStale = false;       // served, but a re-stat is wanted
};

struct Directory
{
    QList<Entry> entries; // always sorted by Entry::file.name
    State state = State::Missing;
    TimePoint fetchedAt = {}; // last successful listing
    QString error;            // meaningful only when Failed (kept through a retry)
};

struct RowRange
{
    int first = 0;
    int count = 0;
};

// What applyListing() changed, as row ranges a list model can replay: drop
// `removed` from the old list (descending, each range as-is), then splice
// `inserted` in at its new-list rows (ascending). `changed` rows are new-list
// rows whose displayed metadata differs from the old row of the same name.
struct Diff
{
    QList<RowRange> removed;
    QList<RowRange> inserted;
    QList<RowRange> changed;

    bool isEmpty() const { return removed.isEmpty() && inserted.isEmpty() && changed.isEmpty(); }
};

struct EntryRef
{
    QString dir; // normalized directory key
    int row = -1; // into Directory::entries
};

// What an invalidation rule touched, for the facade to signal.
struct Invalidation
{
    QStringList staleDirectories; // includes Loading directories that will land Stale
    QList<EntryRef> staleLinkCounts;
};

enum class StatOutcome {
    Applied,
    InodeMismatch, // the file was replaced: count stored but stale, parent Stale
    NotCached,     // parent or name unknown; nothing stored
};

struct StatResult
{
    StatOutcome outcome = StatOutcome::NotCached;
    EntryRef ref;
};

// One consumer's interest in a directory. `priorityNames` (its visible rows)
// are stat'ed before the rest of the directory.
struct Subscription
{
    QString dir;
    QStringList priorityNames;
};

class Store
{
public:
    explicit Store(Clock clock = {}); // empty: steady_clock::now
    Store(const Store &) = delete;    // one cache; a copy would be a bug
    Store &operator=(const Store &) = delete;
    Store(Store &&) = default;
    Store &operator=(Store &&) = default;

    // --- Reads (paths are normalized here) --------------------------------

    const Directory *find(const QString &dir) const; // nullptr when Missing
    State state(const QString &dir) const;
    const Entry *findEntry(const QString &path) const;
    QStringList pathsWithInode(quint64 inode) const; // files only, unordered
    int directoryCount() const;
    qsizetype entryCount() const;

    // --- Fetch lifecycle --------------------------------------------------

    // Creates the record if needed; keeps the old entries so an in-flight
    // refresh still serves them.
    void markLoading(const QString &dir);
    // Lands as Fresh, or Stale if the directory was invalidated while Loading
    // (the reply may predate the mutation). Link counts carry over by inode
    // and are flagged stale; a name whose inode changed starts over Unknown.
    Diff applyListing(const QString &dir, const QList<FileEntry> &entries);
    // Creates the record if needed, like markLoading; entries are kept.
    void applyListFailure(const QString &dir, const QString &message);
    // `nlink` is a real count; failures go through applyStatFailure.
    StatResult applyStat(const QString &path, int nlink, quint64 inode);
    StatResult applyStatFailure(const QString &path); // stores kNlinkUnavailable

    // --- Invalidation rules: bookkeeping only, never a fetch ---------------
    // Each marks only what is cached; a Missing directory is never created.

    // Both parents Stale, plus every cached directory at or under either
    // path (a renamed directory takes its cached subtree with it).
    Invalidation noteRenamed(const QString &from, const QString &to);
    // Both parents Stale; every cached link count of `existingPath`'s inode
    // goes stale.
    Invalidation noteLinked(const QString &existingPath, const QString &newLinkPath);
    // Parent Stale; the inode's other link counts go stale; cached
    // directories at or under `path` go Stale.
    Invalidation noteUnlinked(const QString &path);
    // Every cached directory Stale, every known link count stale.
    void markAllStale();

    // --- Stat ordering ----------------------------------------------------

    // Up to `max` file paths that want a stat (nlink Unknown or stale), never
    // one in `exclude` (the facade's in-flight set). Walks the subscriptions
    // in order; per directory, priority names first, then the rest in stored
    // order; Unknown counts before merely stale ones among the rows examined.
    // A cached failure (Unavailable, not stale) is never retried here.
    QStringList nextStats(const QList<Subscription> &subscriptions,
                          const QSet<QString> &exclude, int max);

    void clear();

private:
    struct Record
    {
        Directory dir;
        bool staleOnLand = false; // invalidated while Loading
        int statCursor = 0;       // first row that may still need a stat
    };

    static int rowOf(const QList<Entry> &entries, const QString &name); // -1 if absent
    static bool needsStat(const Entry &entry);

    Record *findRecord(const QString &key);
    const Record *findRecord(const QString &key) const;
    Entry *findEntryMutable(const QString &path, QString *keyOut, int *rowOut);

    void indexAdd(const QString &key, const Entry &entry);
    void indexRemove(const QString &key, const Entry &entry);

    void markStale(const QString &key, Invalidation *out);
    void markStaleRecord(Record &rec, const QString &key, Invalidation *out);
    void markStaleUnder(const QString &path, Invalidation *out);
    void staleLinkCountsOf(quint64 inode, Invalidation *out);

    Clock m_clock;
    QHash<QString, Record> m_dirs;
    QMultiHash<quint64, QString> m_inodes; // inode -> full path, files only
};

} // namespace sharecache
