#include <QtTest>

#include <QElapsedTimer>

#include "core/ShareCacheStore.h"

#include "common/TestMain.h"

using namespace sharecache;

// Legible QCOMPARE failures for the store's non-Qt types.
namespace QTest {
template <>
inline char *toString(const TimePoint &t)
{
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch());
    return qstrdup(qPrintable(QStringLiteral("t=%1ms").arg(ms.count())));
}
template <>
inline char *toString(const State &s)
{
    static const char *const names[] = {"Missing", "Loading", "Fresh", "Stale", "Failed"};
    return qstrdup(names[int(s)]);
}
template <>
inline char *toString(const StatOutcome &o)
{
    static const char *const names[] = {"Applied", "InodeMismatch", "NotCached"};
    return qstrdup(names[int(o)]);
}
} // namespace QTest

namespace {

// A tick counter stands in for the monotonic clock.
struct FakeClock
{
    qint64 ticks = 0;

    Clock fn()
    {
        return [this] { return TimePoint(std::chrono::milliseconds(ticks)); };
    }

    static TimePoint at(qint64 t) { return TimePoint(std::chrono::milliseconds(t)); }
};

FileEntry file(const char *name, quint64 inode, quint64 size = 0)
{
    FileEntry fe;
    fe.name = QString::fromLatin1(name);
    fe.inode = inode;
    fe.size = size;
    return fe;
}

FileEntry dir(const char *name, quint64 inode)
{
    FileEntry fe = file(name, inode);
    fe.isDir = true;
    return fe;
}

QStringList namesOf(const Store &store, const QString &dir)
{
    QStringList out;
    const Directory *d = store.find(dir);
    if (d) {
        for (const Entry &e : d->entries) {
            out << e.file.name;
        }
    }
    return out;
}

QStringList namesOf(const QList<FileEntry> &entries)
{
    QStringList out;
    for (const FileEntry &fe : entries) {
        out << fe.name;
    }
    std::sort(out.begin(), out.end());
    return out;
}

// The model-side contract: old rows minus `removed` (descending), then
// `inserted` spliced in at ascending new-list rows, equals the new list.
QStringList replay(QStringList old, const Diff &diff, const QStringList &newNames)
{
    for (const RowRange &r : diff.removed) {
        old.remove(r.first, r.count);
    }
    for (const RowRange &r : diff.inserted) {
        for (int k = 0; k < r.count; ++k) {
            old.insert(r.first + k, newNames.at(r.first + k));
        }
    }
    return old;
}

QList<int> rows(const QList<RowRange> &ranges)
{
    QList<int> out;
    for (const RowRange &r : ranges) {
        for (int k = 0; k < r.count; ++k) {
            out << r.first + k;
        }
    }
    return out;
}

QStringList sorted(QStringList list)
{
    std::sort(list.begin(), list.end());
    return list;
}

QList<EntryRef> sortedRefs(QList<EntryRef> refs)
{
    std::sort(refs.begin(), refs.end(), [](const EntryRef &a, const EntryRef &b) {
        return a.dir != b.dir ? a.dir < b.dir : a.row < b.row;
    });
    return refs;
}

} // namespace

class TestShareCacheStore : public QObject
{
    Q_OBJECT

private slots:
    void missingAndStates();
    void diffInsertRemoveChange();
    void diffPureReorder();
    void diffSameNameNewInode();
    void diffReplayInvariant();
    void linkCountCarryOver();
    void inodeIndex();
    void statApply();
    void statInodeMismatch();
    void statFailure();
    void statNotCached();
    void invalidateRename();
    void invalidateLink();
    void invalidateUnlink();
    void invalidateWhileLoadingLandsStale();
    void markAllStaleAndClear();
    void nextStatsOrdering();
    void nextStatsAfterInvalidation();
    void footprint100k();
};

void TestShareCacheStore::missingAndStates()
{
    FakeClock clock;
    Store store(clock.fn());

    QVERIFY(store.find("/a") == nullptr);
    QCOMPARE(store.state("/a"), State::Missing);
    QCOMPARE(store.directoryCount(), 0);

    store.markLoading("/a/");
    QCOMPARE(store.state("/a"), State::Loading);
    QVERIFY(store.find("/a")->entries.isEmpty());
    QCOMPARE(store.directoryCount(), 1);

    clock.ticks = 10;
    store.applyListing("/a", {file("x", 1), file("y", 2)});
    QCOMPARE(store.state("/a"), State::Fresh);
    QCOMPARE(store.find("/a")->fetchedAt, FakeClock::at(10));
    QCOMPARE(store.entryCount(), 2);

    store.applyListFailure("/a", "denied");
    QCOMPARE(store.state("/a"), State::Failed);
    QCOMPARE(store.find("/a")->error, QString("denied"));
    QCOMPARE(namesOf(store, "/a"), QStringList({"x", "y"}));
    QCOMPARE(store.find("/a")->fetchedAt, FakeClock::at(10));

    store.markLoading("/a");
    QCOMPARE(store.state("/a"), State::Loading);
    QCOMPARE(namesOf(store, "/a"), QStringList({"x", "y"}));

    clock.ticks = 20;
    store.applyListing("/a", {file("x", 1)});
    QCOMPARE(store.state("/a"), State::Fresh);
    QVERIFY(store.find("/a")->error.isEmpty());
    QCOMPARE(store.find("/a")->fetchedAt, FakeClock::at(20));

    // A directory listed without markLoading first is fine too.
    store.applyListing("/b", {});
    QCOMPARE(store.state("/b"), State::Fresh);
}

void TestShareCacheStore::diffInsertRemoveChange()
{
    Store store;
    QVERIFY(!store.applyListing("/d", {file("b", 2), file("d", 4), file("f", 6), file("h", 8)})
                 .isEmpty());

    // Remove b and h, insert a, c and e, change d's size, keep f.
    const QList<FileEntry> next = {file("a", 1), file("c", 3), file("d", 4, 99), file("e", 5),
                                   file("f", 6)};
    const Diff diff = store.applyListing("/d", next);

    QCOMPARE(rows(diff.removed), QList<int>({3, 0}));
    QCOMPARE(diff.removed.size(), 2);
    QCOMPARE(rows(diff.inserted), QList<int>({0, 1, 3}));
    QCOMPARE(diff.inserted.size(), 2); // {0,2} and {3,1}
    QCOMPARE(rows(diff.changed), QList<int>({2}));
    QCOMPARE(namesOf(store, "/d"), QStringList({"a", "c", "d", "e", "f"}));
    QCOMPARE(replay({"b", "d", "f", "h"}, diff, namesOf(next)), namesOf(store, "/d"));
    QCOMPARE(store.findEntry("/d/d")->file.size, quint64(99));
}

void TestShareCacheStore::diffPureReorder()
{
    Store store;
    store.applyListing("/d", {file("a", 1), file("b", 2), file("c", 3)});
    const Diff diff = store.applyListing("/d", {file("c", 3), file("a", 1), file("b", 2)});
    QVERIFY(diff.isEmpty());
    QCOMPARE(namesOf(store, "/d"), QStringList({"a", "b", "c"}));

    // Case matters: "B" is a different name from "b", and sorts before "a".
    const Diff caseDiff = store.applyListing("/d", {file("a", 1), file("B", 2), file("c", 3)});
    QCOMPARE(rows(caseDiff.removed), QList<int>({1}));
    QCOMPARE(rows(caseDiff.inserted), QList<int>({0}));
    QCOMPARE(namesOf(store, "/d"), QStringList({"B", "a", "c"}));
}

void TestShareCacheStore::diffSameNameNewInode()
{
    FakeClock clock;
    Store store(clock.fn());
    store.applyListing("/d", {file("a", 1), file("b", 2)});
    store.applyStat("/d/a", 3, 1);
    QCOMPARE(store.pathsWithInode(1), QStringList({"/d/a"}));

    const Diff diff = store.applyListing("/d", {file("a", 7), file("b", 2)});
    QVERIFY(diff.removed.isEmpty());
    QVERIFY(diff.inserted.isEmpty());
    QCOMPARE(rows(diff.changed), QList<int>({0}));

    const Entry *a = store.findEntry("/d/a");
    QCOMPARE(a->file.inode, quint64(7));
    QCOMPARE(a->file.nlink, FileEntry::kNlinkUnknown);
    QVERIFY(!a->nlinkStale);
    QVERIFY(store.pathsWithInode(1).isEmpty());
    QCOMPARE(store.pathsWithInode(7), QStringList({"/d/a"}));
}

void TestShareCacheStore::diffReplayInvariant()
{
    struct Case
    {
        QStringList before;
        QStringList after;
    };
    const QList<Case> cases = {
        {{}, {"a", "b"}},
        {{"a", "b"}, {}},
        {{"c", "d"}, {"a", "b", "c", "d"}},              // insert run at start
        {{"a", "b"}, {"a", "b", "c", "d"}},              // insert run at end
        {{"a", "d"}, {"a", "b", "c", "d"}},              // insert run in the middle
        {{"a", "b", "c", "d"}, {"c", "d"}},              // remove run at start
        {{"a", "b", "c", "d"}, {"a", "b"}},              // remove run at end
        {{"a", "b", "c", "d"}, {"a", "d"}},              // remove run in the middle
        {{"a", "b", "c", "d"}, {"e", "f"}},              // everything replaced
        {{"a", "c", "e", "g"}, {"b", "c", "d", "f", "g"}}, // interleaved
        {{"b", "c", "x", "y"}, {"a", "c", "d", "y", "z"}},
    };
    int n = 0;
    for (const Case &c : cases) {
        Store store;
        QList<FileEntry> before;
        for (const QString &name : c.before) {
            before << file(name.toLatin1().constData(), ++n);
        }
        QList<FileEntry> after;
        for (const QString &name : c.after) {
            after << file(name.toLatin1().constData(), ++n);
        }
        store.applyListing("/r", before);
        const Diff diff = store.applyListing("/r", after);
        QCOMPARE(replay(c.before, diff, c.after), c.after);
        QCOMPARE(namesOf(store, "/r"), c.after);
        // Each removed range is emitted after the ranges above it.
        for (int k = 1; k < diff.removed.size(); ++k) {
            QVERIFY(diff.removed[k].first < diff.removed[k - 1].first);
        }
    }
}

void TestShareCacheStore::linkCountCarryOver()
{
    FakeClock clock;
    Store store(clock.fn());
    store.applyListing("/d", {file("known", 1, 10), file("unknown", 2), file("failed", 3)});
    clock.ticks = 5;
    store.applyStat("/d/known", 2, 1);
    clock.ticks = 6;
    store.applyStatFailure("/d/failed");

    clock.ticks = 50;
    const Diff diff = store.applyListing("/d", {file("known", 1, 11), file("unknown", 2),
                                                file("failed", 3)});
    QVERIFY(diff.removed.isEmpty());
    QVERIFY(diff.inserted.isEmpty());
    QCOMPARE(rows(diff.changed), QList<int>({1})); // sorted: failed, known, unknown

    const Entry *known = store.findEntry("/d/known");
    QCOMPARE(known->file.nlink, 2);
    QCOMPARE(known->nlinkFetchedAt, FakeClock::at(5));
    QVERIFY(known->nlinkStale);
    QCOMPARE(known->file.size, quint64(11));

    const Entry *unknown = store.findEntry("/d/unknown");
    QCOMPARE(unknown->file.nlink, FileEntry::kNlinkUnknown);
    QVERIFY(!unknown->nlinkStale);

    // A cached failure is a known result: carried, and worth a retry.
    const Entry *failed = store.findEntry("/d/failed");
    QCOMPARE(failed->file.nlink, FileEntry::kNlinkUnavailable);
    QCOMPARE(failed->nlinkFetchedAt, FakeClock::at(6));
    QVERIFY(failed->nlinkStale);
}

void TestShareCacheStore::inodeIndex()
{
    Store store;
    store.applyListing("/a", {file("f1", 1), file("f2", 2), dir("sub", 3), file("noinode", 0)});
    store.applyListing("/b", {file("g1", 1), file("g2", 4)});
    QCOMPARE(sorted(store.pathsWithInode(1)), QStringList({"/a/f1", "/b/g1"}));
    QCOMPARE(store.pathsWithInode(2), QStringList({"/a/f2"}));
    QVERIFY(store.pathsWithInode(3).isEmpty()); // directories are not indexed
    QVERIFY(store.pathsWithInode(0).isEmpty());

    // Replace f2's inode, remove f1, flip sub to a file and noinode to a dir.
    store.applyListing("/a", {file("f2", 5), file("sub", 3), dir("noinode", 6)});
    QCOMPARE(store.pathsWithInode(1), QStringList({"/b/g1"}));
    QVERIFY(store.pathsWithInode(2).isEmpty());
    QCOMPARE(store.pathsWithInode(5), QStringList({"/a/f2"}));
    QCOMPARE(store.pathsWithInode(3), QStringList({"/a/sub"}));
    QVERIFY(store.pathsWithInode(6).isEmpty());

    // Flip sub back to a directory: it leaves the index.
    store.applyListing("/a", {file("f2", 5), dir("sub", 3)});
    QVERIFY(store.pathsWithInode(3).isEmpty());

    store.clear();
    QVERIFY(store.pathsWithInode(1).isEmpty());
    QVERIFY(store.pathsWithInode(5).isEmpty());
    QCOMPARE(store.directoryCount(), 0);
    QCOMPARE(store.entryCount(), qsizetype(0));
}

void TestShareCacheStore::statApply()
{
    FakeClock clock;
    Store store(clock.fn());
    store.applyListing("/d", {file("a", 1), file("b", 2)});
    clock.ticks = 7;
    const StatResult r = store.applyStat("/d/b", 4, 2);
    QCOMPARE(r.outcome, StatOutcome::Applied);
    QCOMPARE(r.ref.dir, QString("/d"));
    QCOMPARE(r.ref.row, 1);
    const Entry *b = store.findEntry("/d/b");
    QCOMPARE(b->file.nlink, 4);
    QCOMPARE(b->nlinkFetchedAt, FakeClock::at(7));
    QVERIFY(!b->nlinkStale);
    QCOMPARE(store.state("/d"), State::Fresh);

    // A reply carrying no inode is applied as-is.
    QCOMPARE(store.applyStat("/d/a", 1, 0).outcome, StatOutcome::Applied);
    QCOMPARE(store.findEntry("/d/a")->file.nlink, 1);
}

void TestShareCacheStore::statInodeMismatch()
{
    FakeClock clock;
    Store store(clock.fn());
    store.applyListing("/d", {file("a", 1)});
    clock.ticks = 3;
    const StatResult r = store.applyStat("/d/a", 2, 9);
    QCOMPARE(r.outcome, StatOutcome::InodeMismatch);
    QCOMPARE(r.ref.row, 0);
    const Entry *a = store.findEntry("/d/a");
    QCOMPARE(a->file.nlink, 2);
    QCOMPARE(a->nlinkFetchedAt, FakeClock::at(3));
    QVERIFY(a->nlinkStale);
    QCOMPARE(a->file.inode, quint64(1)); // the re-list brings the new inode
    QCOMPARE(store.state("/d"), State::Stale);
}

void TestShareCacheStore::statFailure()
{
    FakeClock clock;
    Store store(clock.fn());
    store.applyListing("/d", {file("a", 1)});
    clock.ticks = 4;
    const StatResult r = store.applyStatFailure("/d/a");
    QCOMPARE(r.outcome, StatOutcome::Applied);
    const Entry *a = store.findEntry("/d/a");
    QCOMPARE(a->file.nlink, FileEntry::kNlinkUnavailable);
    QCOMPARE(a->nlinkFetchedAt, FakeClock::at(4));
    QVERIFY(!a->nlinkStale);
    QCOMPARE(store.state("/d"), State::Fresh);
}

void TestShareCacheStore::statNotCached()
{
    Store store;
    store.applyListing("/d", {file("a", 1)});
    QCOMPARE(store.applyStat("/d/zzz", 1, 1).outcome, StatOutcome::NotCached);
    QCOMPARE(store.applyStat("/other/a", 1, 1).outcome, StatOutcome::NotCached);
    QCOMPARE(store.applyStatFailure("/d/zzz").outcome, StatOutcome::NotCached);
    QCOMPARE(store.applyStat("/", 1, 1).outcome, StatOutcome::NotCached);
    QCOMPARE(store.directoryCount(), 1);
    QVERIFY(store.findEntry("/d/zzz") == nullptr);
}

void TestShareCacheStore::invalidateRename()
{
    Store store;
    store.applyListing("/", {dir("src", 1), dir("dst", 2), dir("other", 3)});
    store.applyListing("/src", {dir("tree", 4), file("f", 5)});
    store.applyListing("/src/tree", {file("g", 6)});
    store.applyListing("/src/treehouse", {file("h", 7)}); // a prefix, not a child
    store.applyListing("/other", {});

    // Rename the directory /src/tree to /dst/tree: both parents and the old
    // subtree go Stale; /dst is Missing and stays that way.
    const Invalidation inv = store.noteRenamed("/src/tree", "/dst/tree");
    QCOMPARE(sorted(inv.staleDirectories), QStringList({"/src", "/src/tree"}));
    QVERIFY(inv.staleLinkCounts.isEmpty());
    QCOMPARE(store.state("/src"), State::Stale);
    QCOMPARE(store.state("/src/tree"), State::Stale);
    QCOMPARE(store.state("/src/treehouse"), State::Fresh);
    QCOMPARE(store.state("/dst"), State::Missing);
    QCOMPARE(store.state("/"), State::Fresh);
    QCOMPARE(store.state("/other"), State::Fresh);

    // A file renamed within one directory touches that directory once.
    store.applyListing("/src", {file("f", 5)});
    const Invalidation same = store.noteRenamed("/src/f", "/src/f2");
    QCOMPARE(same.staleDirectories, QStringList({"/src"}));
    QCOMPARE(store.state("/src"), State::Stale);
    QCOMPARE(namesOf(store, "/src"), QStringList({"f"})); // entries are kept

    // Renaming into a cached destination directory stales it too.
    const Invalidation cross = store.noteRenamed("/src/f", "/other/f");
    QCOMPARE(sorted(cross.staleDirectories), QStringList({"/other", "/src"}));
    QCOMPARE(store.state("/other"), State::Stale);
}

void TestShareCacheStore::invalidateLink()
{
    Store store;
    store.applyListing("/a", {file("x", 1), file("y", 2)});
    store.applyListing("/b", {file("z", 1), file("w", 3)});
    store.applyListing("/c", {file("v", 1)});
    store.applyStat("/a/x", 2, 1);
    store.applyStat("/b/z", 2, 1);
    store.applyStat("/a/y", 1, 2);
    // /c/v shares the inode but its count is still Unknown.

    const Invalidation inv = store.noteLinked("/a/x", "/b/x");
    QCOMPARE(sorted(inv.staleDirectories), QStringList({"/a", "/b"}));
    const QList<EntryRef> refs = sortedRefs(inv.staleLinkCounts);
    QCOMPARE(refs.size(), 2);
    QCOMPARE(refs[0].dir, QString("/a"));
    QCOMPARE(refs[0].row, 0); // x
    QCOMPARE(refs[1].dir, QString("/b"));
    QCOMPARE(refs[1].row, 1); // sorted: w, z
    QVERIFY(store.findEntry("/a/x")->nlinkStale);
    QVERIFY(store.findEntry("/b/z")->nlinkStale);
    QVERIFY(!store.findEntry("/a/y")->nlinkStale);
    QVERIFY(!store.findEntry("/c/v")->nlinkStale);
    QCOMPARE(store.findEntry("/c/v")->file.nlink, FileEntry::kNlinkUnknown);
    QCOMPARE(store.state("/a"), State::Stale);
    QCOMPARE(store.state("/b"), State::Stale);
    QCOMPARE(store.state("/c"), State::Fresh);

    // An existing path that is not cached still stales the parents it can.
    const Invalidation unknownSource = store.noteLinked("/nowhere/q", "/c/q");
    QCOMPARE(unknownSource.staleDirectories, QStringList({"/c"}));
    QVERIFY(unknownSource.staleLinkCounts.isEmpty());
    QCOMPARE(store.state("/c"), State::Stale);
    QCOMPARE(store.state("/nowhere"), State::Missing);
}

void TestShareCacheStore::invalidateUnlink()
{
    Store store;
    store.applyListing("/a", {file("x", 1), file("y", 2), dir("d", 9)});
    store.applyListing("/b", {file("z", 1)});
    store.applyListing("/a/d", {file("inner", 5)});
    store.applyStat("/a/x", 2, 1);
    store.applyStat("/b/z", 2, 1);

    const Invalidation inv = store.noteUnlinked("/a/x");
    QCOMPARE(inv.staleDirectories, QStringList({"/a"}));
    QCOMPARE(inv.staleLinkCounts.size(), 2);
    QVERIFY(store.findEntry("/b/z")->nlinkStale);
    QCOMPARE(store.state("/a"), State::Stale);
    QCOMPARE(store.state("/b"), State::Fresh);
    QCOMPARE(store.state("/a/d"), State::Fresh);
    QCOMPARE(namesOf(store, "/a"), QStringList({"d", "x", "y"}));

    // Removing a directory takes its cached listing to Stale as well.
    const Invalidation rmdir = store.noteUnlinked("/a/d");
    QCOMPARE(sorted(rmdir.staleDirectories), QStringList({"/a", "/a/d"}));
    QCOMPARE(store.state("/a/d"), State::Stale);
}

void TestShareCacheStore::invalidateWhileLoadingLandsStale()
{
    Store store;
    store.applyListing("/a", {file("x", 1)});
    store.markLoading("/a");
    const Invalidation inv = store.noteRenamed("/a/x", "/a/y");
    QCOMPARE(inv.staleDirectories, QStringList({"/a"}));
    QCOMPARE(store.state("/a"), State::Loading); // still serving the old rows
    QCOMPARE(namesOf(store, "/a"), QStringList({"x"}));

    store.applyListing("/a", {file("y", 1)});
    QCOMPARE(store.state("/a"), State::Stale);

    // The flag is consumed: the next landing is Fresh.
    store.markLoading("/a");
    store.applyListing("/a", {file("y", 1)});
    QCOMPARE(store.state("/a"), State::Fresh);

    // A failure while flagged does not carry the flag to a later success.
    store.markLoading("/a");
    store.noteUnlinked("/a/y");
    store.applyListFailure("/a", "boom");
    QCOMPARE(store.state("/a"), State::Failed);
    store.markLoading("/a");
    store.applyListing("/a", {});
    QCOMPARE(store.state("/a"), State::Fresh);
}

void TestShareCacheStore::markAllStaleAndClear()
{
    Store store;
    store.applyListing("/a", {file("x", 1), file("y", 2), dir("d", 3)});
    store.applyListing("/b", {file("z", 4)});
    store.markLoading("/b");
    store.applyStat("/a/x", 1, 1);
    store.applyStatFailure("/a/y");
    QCOMPARE(store.directoryCount(), 2);
    QCOMPARE(store.entryCount(), qsizetype(4));

    store.markAllStale();
    QCOMPARE(store.state("/a"), State::Stale);
    QCOMPARE(store.state("/b"), State::Loading);
    QVERIFY(store.findEntry("/a/x")->nlinkStale);
    QVERIFY(store.findEntry("/a/y")->nlinkStale);
    QVERIFY(!store.findEntry("/a/d")->nlinkStale);
    QVERIFY(!store.findEntry("/b/z")->nlinkStale); // Unknown stays Unknown
    store.applyListing("/b", {file("z", 4)});
    QCOMPARE(store.state("/b"), State::Stale);

    store.clear();
    QCOMPARE(store.directoryCount(), 0);
    QCOMPARE(store.entryCount(), qsizetype(0));
    QCOMPARE(store.state("/a"), State::Missing);
    QVERIFY(store.pathsWithInode(1).isEmpty());
}

void TestShareCacheStore::nextStatsOrdering()
{
    Store store;
    store.applyListing("/a", {file("a1", 1), file("a2", 2), file("a3", 3), file("a4", 4),
                              dir("sub", 5), file("a5", 6)});
    store.applyListing("/b", {file("b1", 7), file("b2", 8)});
    store.applyStat("/a/a2", 1, 2);       // resolved
    store.applyStatFailure("/a/a3");      // cached failure: never retried
    store.applyStat("/a/a4", 2, 4);
    store.noteLinked("/a/a4", "/x/a4");   // a4 is now stale (and /a Stale)

    // Priority names first (Unknown before stale within them), then the rest.
    const QList<Subscription> subs = {{"/a", {"a4", "a5", "a3", "nope"}}, {"/b", {}}};
    QCOMPARE(store.nextStats(subs, {}, 10),
             QStringList({"/a/a5", "/a/a4", "/a/a1", "/b/b1", "/b/b2"}));

    // `max` is honoured and excluded paths are skipped.
    QCOMPARE(store.nextStats(subs, {"/a/a5"}, 2), QStringList({"/a/a4", "/a/a1"}));
    QCOMPARE(store.nextStats(subs, {"/a/a5", "/a/a4", "/a/a1"}, 10),
             QStringList({"/b/b1", "/b/b2"}));

    // Subscription order decides directory order.
    const QList<Subscription> reversed = {{"/b", {"b2"}}, {"/a", {}}};
    QCOMPARE(store.nextStats(reversed, {}, 3), QStringList({"/b/b2", "/b/b1", "/a/a1"}));

    // Missing directories and empty ones contribute nothing.
    store.markLoading("/c");
    QVERIFY(store.nextStats({{"/zzz", {}}, {"/c", {}}}, {}, 10).isEmpty());
    QVERIFY(store.nextStats(subs, {}, 0).isEmpty());
}

void TestShareCacheStore::nextStatsAfterInvalidation()
{
    Store store;
    store.applyListing("/a", {file("x", 1), file("y", 2)});
    store.applyListing("/b", {file("z", 1)});
    const QList<Subscription> subs = {{"/a", {}}, {"/b", {}}};

    QCOMPARE(store.nextStats(subs, {}, 10), QStringList({"/a/x", "/a/y", "/b/z"}));
    store.applyStat("/a/x", 2, 1);
    store.applyStat("/a/y", 1, 2);
    store.applyStat("/b/z", 2, 1);
    QVERIFY(store.nextStats(subs, {}, 10).isEmpty());

    // The cursor has passed every row; a stale mark must bring them back.
    store.noteLinked("/b/z", "/b/z2");
    QCOMPARE(store.nextStats(subs, {}, 10), QStringList({"/a/x", "/b/z"}));
    store.applyStat("/a/x", 3, 1);
    store.applyStat("/b/z", 3, 1);
    QVERIFY(store.nextStats(subs, {}, 10).isEmpty());

    // A stale listing alone changes no row's stat status.
    store.noteRenamed("/a/x", "/a/x2");
    QVERIFY(store.nextStats(subs, {}, 10).isEmpty());

    // A re-list with a carried count makes the rows stale again; an
    // inode-mismatch stat keeps its row a candidate.
    store.applyListing("/a", {file("x", 1), file("y", 2)});
    QCOMPARE(store.nextStats(subs, {}, 10), QStringList({"/a/x", "/a/y"}));
    store.applyStat("/a/x", 1, 1);
    store.applyStat("/a/y", 1, 99);
    QCOMPARE(store.nextStats(subs, {}, 10), QStringList({"/a/y"}));

    store.markAllStale();
    QCOMPARE(store.nextStats(subs, {}, 10), QStringList({"/a/x", "/a/y", "/b/z"}));
}

void TestShareCacheStore::footprint100k()
{
    QVERIFY2(sizeof(Entry) <= 96, qPrintable(QString::number(sizeof(Entry))));

    constexpr int kCount = 100000;
    QList<FileEntry> entries;
    entries.reserve(kCount);
    for (int i = 0; i < kCount; ++i) {
        entries << file(qPrintable(QStringLiteral("file-%1.bin").arg(i, 6, 10, QLatin1Char('0'))),
                        quint64(i + 1), quint64(i));
    }

    Store store;
    QElapsedTimer timer;
    timer.start();
    const Diff first = store.applyListing("/big", entries);
    const qint64 firstMs = timer.elapsed();
    QCOMPARE(rows(first.inserted).size(), kCount);
    QCOMPARE(store.entryCount(), qsizetype(kCount));

    std::reverse(entries.begin(), entries.end());
    timer.restart();
    const Diff second = store.applyListing("/big", entries);
    const qint64 secondMs = timer.elapsed();
    QVERIFY(second.isEmpty());
    QCOMPARE(store.entryCount(), qsizetype(kCount));
    QCOMPARE(store.pathsWithInode(kCount), QStringList({"/big/file-099999.bin"}));

    // The stat pump over a directory this size must not rescan resolved rows.
    timer.restart();
    const QList<Subscription> subs = {{"/big", {}}};
    int pumped = 0;
    QSet<QString> inFlight;
    for (;;) {
        const QStringList batch = store.nextStats(subs, inFlight, 64);
        if (batch.isEmpty()) {
            break;
        }
        for (const QString &path : batch) {
            store.applyStat(path, 1, store.findEntry(path)->file.inode);
        }
        pumped += batch.size();
    }
    const qint64 pumpMs = timer.elapsed();
    QCOMPARE(pumped, kCount);

    qInfo("100k entries: first listing %lld ms, reversed re-list %lld ms, stat pump %lld ms",
          firstMs, secondMs, pumpMs);
}

SMBMGR_TEST_MAIN(TestShareCacheStore)

#include "tst_sharecachestore.moc"
