#include <QtTest>

#include "core/MatchSearcher.h"
#include "smb/SmbSession.h"

#include "common/SmbFixture.h"
#include "common/TestMain.h"

// The recursive enumeration + pairing pipeline against the Samba fixture (the
// panel UI on top is covered by tst_matchfinderpanel). The pure pairing math
// has its own serverless suite (tst_matchpairing).
class TestMatchSearcher : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void basicSearch();
    void identicalRootsProduceNoSelfPairs();
    void nestedRootsListOverlapOnce();
    void nonRecursiveSideStaysShallow();
    void sizeOptions();
    void cancelMidSearch();
    void nonexistentRootFails();
    void unlistableSubfolderCountsAsError();

private:
    struct SearchResult
    {
        QList<MatchSearcher::Match> matches;
        int folderErrors = 0;
        bool truncated = false;
        bool cancelled = false;
        bool failed = false;
        QString failureMessage;
        int foldersListed = 0; // from the last progress signal
        bool done = false;
    };
    SearchResult search(SmbSession &session, const MatchSearcher::Options &options,
                        int timeoutMs = 30000);

    static QSet<QString> pairSet(const QList<MatchSearcher::Match> &matches);

    SmbFixture m_fx;
};

void TestMatchSearcher::initTestCase()
{
    SmbSession session;
    SMBMGR_CONNECT_OR_SKIP(m_fx, session);
}

TestMatchSearcher::SearchResult TestMatchSearcher::search(
    SmbSession &session, const MatchSearcher::Options &options, int timeoutMs)
{
    MatchSearcher searcher(&session);
    SearchResult result;
    connect(&searcher, &MatchSearcher::progress, this,
            [&result](int foldersListed, int, int) {
                result.foldersListed = foldersListed;
            });
    connect(&searcher, &MatchSearcher::finished, this,
            [&result](const QList<MatchSearcher::Match> &matches, int folderErrors,
                      bool truncated, bool cancelled) {
                result.matches = matches;
                result.folderErrors = folderErrors;
                result.truncated = truncated;
                result.cancelled = cancelled;
                result.done = true;
            });
    connect(&searcher, &MatchSearcher::failed, this,
            [&result](const QString &message) {
                result.failed = true;
                result.failureMessage = message;
                result.done = true;
            });

    searcher.start(options);
    [&] { QTRY_VERIFY_WITH_TIMEOUT(result.done, timeoutMs); }();
    return result;
}

QSet<QString> TestMatchSearcher::pairSet(const QList<MatchSearcher::Match> &matches)
{
    QSet<QString> pairs;
    for (const MatchSearcher::Match &match : matches) {
        pairs.insert(match.primaryPath + "|" + match.secondaryPath);
    }
    return pairs;
}

void TestMatchSearcher::basicSearch()
{
    const QString dir = m_fx.makeCaseDir("basic");
    m_fx.makeDir(dir + "/p/sub");
    m_fx.makeDir(dir + "/s");
    m_fx.seedFile(dir + "/p", "a.bin", 1000);
    m_fx.seedFile(dir + "/p/sub", "b.bin", 1000);
    m_fx.seedFile(dir + "/s", "x.bin", 1000);
    m_fx.seedFile(dir + "/s", "other.bin", 2000);
    // A pre-linked cross-side pair: same inode, must be absent from results.
    m_fx.seedFile(dir + "/p", "linked.bin", 500);
    m_fx.makeHardLink(dir + "/p/linked.bin", dir + "/s/linked.bin");

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));

    MatchSearcher::Options options;
    options.primaryPath = dir + "/p";
    options.secondaryPath = dir + "/s";
    const SearchResult result = search(session, options);

    QVERIFY(!result.failed);
    QVERIFY(!result.cancelled);
    QCOMPARE(result.folderErrors, 0);
    QCOMPARE(pairSet(result.matches),
             QSet<QString>({dir + "/p/a.bin|" + dir + "/s/x.bin",
                            dir + "/p/sub/b.bin|" + dir + "/s/x.bin"}));
}

// Overlapping roots, first case: Primary == Secondary.
void TestMatchSearcher::identicalRootsProduceNoSelfPairs()
{
    const QString dir = m_fx.makeCaseDir("identicalroots");
    m_fx.seedFile(dir, "a.bin", 300);
    m_fx.seedFile(dir, "b.bin", 300);

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));

    MatchSearcher::Options options;
    options.primaryPath = dir;
    options.secondaryPath = dir;
    const SearchResult result = search(session, options);

    // One row for the unordered pair — no self-pairs, no A/B + B/A doubles.
    QCOMPARE(result.matches.size(), 1);
    QVERIFY(result.matches.first().primaryPath != result.matches.first().secondaryPath);
    // The shared root was listed exactly once.
    QCOMPARE(result.foldersListed, 1);
}

// Overlapping roots, second case: Secondary nested inside a recursive Primary
// — the overlap is traversed once.
void TestMatchSearcher::nestedRootsListOverlapOnce()
{
    const QString dir = m_fx.makeCaseDir("nestedroots");
    m_fx.makeDir(dir + "/sub");
    m_fx.seedFile(dir, "f1.bin", 400);
    m_fx.seedFile(dir + "/sub", "f2.bin", 400);

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));

    MatchSearcher::Options options;
    options.primaryPath = dir;
    options.secondaryPath = dir + "/sub";
    const SearchResult result = search(session, options);

    QCOMPARE(pairSet(result.matches),
             QSet<QString>({dir + "/f1.bin|" + dir + "/sub/f2.bin"}));
    // dir and dir/sub each listed once — the nested root not a second time.
    QCOMPARE(result.foldersListed, 2);
}

void TestMatchSearcher::nonRecursiveSideStaysShallow()
{
    const QString dir = m_fx.makeCaseDir("nonrecursive");
    m_fx.makeDir(dir + "/p/deep");
    m_fx.makeDir(dir + "/s");
    m_fx.seedFile(dir + "/p", "top.bin", 200);
    m_fx.seedFile(dir + "/p/deep", "nested.bin", 200);
    m_fx.seedFile(dir + "/s", "x.bin", 200);

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));

    MatchSearcher::Options options;
    options.primaryPath = dir + "/p";
    options.primaryRecursive = false;
    options.secondaryPath = dir + "/s";
    const SearchResult result = search(session, options);

    // Only the primary root's direct files are considered.
    QCOMPARE(pairSet(result.matches),
             QSet<QString>({dir + "/p/top.bin|" + dir + "/s/x.bin"}));
}

// Size Min excludes small files; a nonzero Size Difference admits near-size
// pairs that an exact search rejects. (The window math itself is unit-tested
// in tst_matchpairing.)
void TestMatchSearcher::sizeOptions()
{
    const QString dir = m_fx.makeCaseDir("sizeoptions");
    m_fx.makeDir(dir + "/p");
    m_fx.makeDir(dir + "/s");
    m_fx.seedFile(dir + "/p", "big.bin", 100);
    m_fx.seedFile(dir + "/s", "near.bin", 105);
    m_fx.seedFile(dir + "/p", "tiny.bin", 10);
    m_fx.seedFile(dir + "/s", "tiny2.bin", 10);

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));

    MatchSearcher::Options options;
    options.primaryPath = dir + "/p";
    options.secondaryPath = dir + "/s";

    // Exact sizes with a 50-byte minimum: 100 vs 105 is too far apart, and the
    // equal-size tiny pair falls below the minimum, so nothing pairs.
    options.sizeMinBytes = 50;
    options.sizeDiffBytes = 0;
    QVERIFY(search(session, options).matches.isEmpty());

    // Admit the tiny pair by dropping the minimum.
    options.sizeMinBytes = 0;
    QCOMPARE(pairSet(search(session, options).matches),
             QSet<QString>({dir + "/p/tiny.bin|" + dir + "/s/tiny2.bin"}));

    // A small nonzero difference adds the near-size pair.
    options.sizeMinBytes = 50;
    options.sizeDiffBytes = 5;
    QCOMPARE(pairSet(search(session, options).matches),
             QSet<QString>({dir + "/p/big.bin|" + dir + "/s/near.bin"}));
}

// Cancelling emits finished(cancelled=true) once, and late listing replies are
// dropped silently.
void TestMatchSearcher::cancelMidSearch()
{
    const QString dir = m_fx.makeCaseDir("cancel");
    m_fx.seedManyDirs(dir, 30, 64);

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));

    MatchSearcher searcher(&session);
    QSignalSpy finishedSpy(&searcher, &MatchSearcher::finished);
    QSignalSpy progressSpy(&searcher, &MatchSearcher::progress);

    MatchSearcher::Options options;
    options.primaryPath = dir;
    options.secondaryPath = dir;
    searcher.start(options);

    // Cancel while listings are genuinely in flight.
    QTRY_VERIFY(progressSpy.count() >= 1);
    QVERIFY(searcher.isRunning());
    searcher.cancel();

    QCOMPARE(finishedSpy.count(), 1);
    QVERIFY(finishedSpy.first().at(3).toBool()); // cancelled
    QVERIFY(!searcher.isRunning());

    // Late replies from the dropped listings must not produce more signals.
    const int progressSoFar = progressSpy.count();
    QTest::qWait(750);
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(progressSpy.count(), progressSoFar);
}

// An unlistable root fails the whole search.
void TestMatchSearcher::nonexistentRootFails()
{
    const QString dir = m_fx.makeCaseDir("badroot");

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));

    MatchSearcher::Options options;
    options.primaryPath = dir + "/missing";
    options.secondaryPath = dir;
    const SearchResult result = search(session, options);

    QVERIFY(result.failed);
    QVERIFY(result.failureMessage.contains(dir + "/missing"));
}

// An unlistable subfolder is counted but doesn't kill the search.
void TestMatchSearcher::unlistableSubfolderCountsAsError()
{
    const QString dir = m_fx.makeCaseDir("badsub");
    m_fx.makeDir(dir + "/p/locked");
    m_fx.makeDir(dir + "/s");
    m_fx.seedFile(dir + "/p", "a.bin", 700);
    m_fx.seedFile(dir + "/s", "b.bin", 700);
    m_fx.seedFile(dir + "/p/locked", "hidden.bin", 700);
    m_fx.chmodPath(dir + "/p/locked", "000");

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));

    MatchSearcher::Options options;
    options.primaryPath = dir + "/p";
    options.secondaryPath = dir + "/s";
    const SearchResult result = search(session, options);
    m_fx.chmodPath(dir + "/p/locked", "755");

    QVERIFY(!result.failed);
    QCOMPARE(result.folderErrors, 1);
    // The listable part still produced its match.
    QCOMPARE(pairSet(result.matches),
             QSet<QString>({dir + "/p/a.bin|" + dir + "/s/b.bin"}));
}

SMBMGR_TEST_MAIN(TestMatchSearcher)

#include "tst_matchsearcher.moc"
