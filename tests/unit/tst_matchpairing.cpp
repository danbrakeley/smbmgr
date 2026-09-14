#include <QtTest>

#include "core/MatchPairing.h"

#include "common/TestMain.h"

using matchpairing::FileRecord;
using matchpairing::kPrimary;
using matchpairing::kSecondary;
using matchpairing::Result;

class TestMatchPairing : public QObject
{
    Q_OBJECT

private slots:
    void exactSizeOnly();
    void sizeWindowBoundary();
    void sameInodeDropped();
    void zeroInodeNeverTreatedAsLinked();
    void samePathNoSelfPair();
    void flippedOrientation();
    void overlapPairedOnce();
    void sameSideOnlyNoPair();
    void truncatesAtMaxMatches();
    void truncatesAtMaxPairsExamined();
    void sortedBySavings();
    void tmpNames();
};

// sizeDiff 0 pairs exact sizes only.
void TestMatchPairing::exactSizeOnly()
{
    const Result r = matchpairing::computeMatches(
        {
            {"/p/a", 100, 1, kPrimary},
            {"/s/a", 100, 2, kSecondary},
            {"/s/b", 101, 3, kSecondary},
        },
        /*sizeDiffBytes*/ 0);
    QVERIFY(!r.truncated);
    QCOMPARE(r.matches.size(), 1);
    QCOMPARE(r.matches.first().primaryPath, "/p/a");
    QCOMPARE(r.matches.first().secondaryPath, "/s/a");
}

// A difference of exactly sizeDiffBytes is inside the window; one more byte
// is outside.
void TestMatchPairing::sizeWindowBoundary()
{
    const Result inside = matchpairing::computeMatches(
        {{"/p/a", 100, 1, kPrimary}, {"/s/b", 110, 2, kSecondary}}, 10);
    QCOMPARE(inside.matches.size(), 1);

    const Result outside = matchpairing::computeMatches(
        {{"/p/a", 100, 1, kPrimary}, {"/s/b", 111, 2, kSecondary}}, 10);
    QCOMPARE(outside.matches.size(), 0);
}

void TestMatchPairing::sameInodeDropped()
{
    const Result r = matchpairing::computeMatches(
        {{"/p/a", 100, 7, kPrimary}, {"/s/b", 100, 7, kSecondary}}, 0);
    QCOMPARE(r.matches.size(), 0);
}

// Inode 0 means "unknown", never "same file".
void TestMatchPairing::zeroInodeNeverTreatedAsLinked()
{
    const Result r = matchpairing::computeMatches(
        {{"/p/a", 100, 0, kPrimary}, {"/s/b", 100, 0, kSecondary}}, 0);
    QCOMPARE(r.matches.size(), 1);
}

void TestMatchPairing::samePathNoSelfPair()
{
    const Result r = matchpairing::computeMatches(
        {{"/x/a", 100, 1, kPrimary | kSecondary},
         {"/x/a", 100, 1, kPrimary | kSecondary}},
        0);
    QCOMPARE(r.matches.size(), 0);
}

// When only the flipped orientation satisfies the side masks, it is used.
void TestMatchPairing::flippedOrientation()
{
    const Result r = matchpairing::computeMatches(
        {{"/s/small", 100, 1, kSecondary}, {"/p/big", 105, 2, kPrimary}}, 10);
    QCOMPARE(r.matches.size(), 1);
    QCOMPARE(r.matches.first().primaryPath, "/p/big");
    QCOMPARE(r.matches.first().primarySize, 105ULL);
    QCOMPARE(r.matches.first().secondaryPath, "/s/small");
    QCOMPARE(r.matches.first().secondarySize, 100ULL);
}

// Files in the overlap of both trees qualify either way — exactly one row per
// unordered pair, with the size-sorted first file as primary.
void TestMatchPairing::overlapPairedOnce()
{
    const Result r = matchpairing::computeMatches(
        {{"/both/a", 100, 1, kPrimary | kSecondary},
         {"/both/b", 105, 2, kPrimary | kSecondary}},
        10);
    QCOMPARE(r.matches.size(), 1);
    QCOMPARE(r.matches.first().primaryPath, "/both/a");
    QCOMPARE(r.matches.first().secondaryPath, "/both/b");
}

void TestMatchPairing::sameSideOnlyNoPair()
{
    const Result primaries = matchpairing::computeMatches(
        {{"/p/a", 100, 1, kPrimary}, {"/p/b", 100, 2, kPrimary}}, 0);
    QCOMPARE(primaries.matches.size(), 0);

    const Result secondaries = matchpairing::computeMatches(
        {{"/s/a", 100, 1, kSecondary}, {"/s/b", 100, 2, kSecondary}}, 0);
    QCOMPARE(secondaries.matches.size(), 0);
}

void TestMatchPairing::truncatesAtMaxMatches()
{
    const Result r = matchpairing::computeMatches(
        {
            {"/p/a", 100, 1, kPrimary},
            {"/s/b", 100, 2, kSecondary},
            {"/s/c", 100, 3, kSecondary},
        },
        0, /*maxMatches*/ 1);
    QVERIFY(r.truncated);
    QCOMPARE(r.matches.size(), 1);
}

void TestMatchPairing::truncatesAtMaxPairsExamined()
{
    const Result r = matchpairing::computeMatches(
        {
            {"/p/a", 100, 1, kPrimary},
            {"/s/b", 100, 2, kSecondary},
            {"/s/c", 100, 3, kSecondary},
        },
        0, matchpairing::kMaxMatches, /*maxPairsExamined*/ 1);
    QVERIFY(r.truncated);
    QVERIFY(r.matches.size() <= 1);
}

// Biggest potential savings first.
void TestMatchPairing::sortedBySavings()
{
    const Result r = matchpairing::computeMatches(
        {
            {"/p/small", 10, 1, kPrimary},
            {"/s/small", 10, 2, kSecondary},
            {"/p/big", 100, 3, kPrimary},
            {"/s/big", 100, 4, kSecondary},
        },
        0);
    QCOMPARE(r.matches.size(), 2);
    QCOMPARE(r.matches.at(0).primarySize, 100ULL);
    QCOMPARE(r.matches.at(1).primarySize, 10ULL);
}

void TestMatchPairing::tmpNames()
{
    QVERIFY(matchpairing::isTmpName("foo.smbmgr-tmp"));
    QVERIFY(matchpairing::isTmpName(".smbmgr-tmp"));
    QVERIFY(!matchpairing::isTmpName("foo.txt"));
    QVERIFY(!matchpairing::isTmpName("foo.smbmgr-tmp.bak"));
}

SMBMGR_TEST_MAIN(TestMatchPairing)

#include "tst_matchpairing.moc"
