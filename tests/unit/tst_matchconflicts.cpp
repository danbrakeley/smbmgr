#include <QtTest>

#include "core/MatchConflicts.h"

#include "common/TestMain.h"

using matchpairing::Match;

namespace {
Match pair(const char *primary, const char *secondary)
{
    return {QString::fromLatin1(primary), QString::fromLatin1(secondary), 0, 0};
}
} // namespace

class TestMatchConflicts : public QObject
{
    Q_OBJECT

private slots:
    void cleanSelection();
    void duplicateSecondary();
    void primaryAlsoSecondary();
    void sharedPrimaryIsAllowed();
    void combination();
};

void TestMatchConflicts::cleanSelection()
{
    const auto c = matchconflicts::find({pair("/p/a", "/s/a"), pair("/p/b", "/s/b")});
    QVERIFY(c.isEmpty());
}

void TestMatchConflicts::duplicateSecondary()
{
    const auto c = matchconflicts::find(
        {pair("/p/a", "/s/x"), pair("/p/b", "/s/x"), pair("/p/c", "/s/x")});
    QVERIFY(!c.isEmpty());
    // One entry per extra occurrence beyond the first.
    QCOMPARE(c.duplicateSecondaries, QStringList({"/s/x", "/s/x"}));
    QVERIFY(c.primaryAndSecondary.isEmpty());
}

void TestMatchConflicts::primaryAlsoSecondary()
{
    // Chained rows: /mid is kept by the first job and replaced by the second.
    const auto c = matchconflicts::find({pair("/mid", "/s/a"), pair("/p/b", "/mid")});
    QVERIFY(!c.isEmpty());
    QVERIFY(c.duplicateSecondaries.isEmpty());
    QCOMPARE(c.primaryAndSecondary, QStringList({"/mid"}));
}

// The same primary keeping several secondaries is the normal multi-victim
// case, not a conflict.
void TestMatchConflicts::sharedPrimaryIsAllowed()
{
    const auto c = matchconflicts::find({pair("/p/a", "/s/x"), pair("/p/a", "/s/y")});
    QVERIFY(c.isEmpty());
}

void TestMatchConflicts::combination()
{
    const auto c = matchconflicts::find(
        {pair("/mid", "/s/x"), pair("/p/b", "/s/x"), pair("/p/c", "/mid")});
    QCOMPARE(c.duplicateSecondaries, QStringList({"/s/x"}));
    QCOMPARE(c.primaryAndSecondary, QStringList({"/mid"}));
}

SMBMGR_TEST_MAIN(TestMatchConflicts)

#include "tst_matchconflicts.moc"
