#include <QtTest>

#include "core/VersionCompare.h"

#include "common/TestMain.h"

class TestVersionCompare : public QObject
{
    Q_OBJECT

private slots:
    void isNewer_data();
    void isNewer();
};

void TestVersionCompare::isNewer_data()
{
    QTest::addColumn<QString>("latestTag");
    QTest::addColumn<QString>("currentVersion");
    QTest::addColumn<bool>("expected");

    QTest::newRow("newer patch")        << "v0.2.3"  << "0.2.2"              << true;
    QTest::newRow("newer minor")        << "v0.3.0"  << "0.2.9"              << true;
    QTest::newRow("newer major")        << "v1.0.0"  << "0.9.9"              << true;
    QTest::newRow("same version")       << "v0.2.2"  << "0.2.2"              << false;
    QTest::newRow("older tag")          << "v0.2.1"  << "0.2.2"              << false;
    QTest::newRow("dev build suffix")   << "v0.2.2"  << "0.2.2-abcd123-dev"  << false;
    QTest::newRow("ahead of tag suffix") << "v0.2.2" << "0.2.2-abcd123"      << false;
    QTest::newRow("dev build behind")   << "v0.3.0"  << "0.2.2-abcd123-dev"  << true;
    QTest::newRow("no v prefix")        << "0.2.3"   << "0.2.2"              << true;
    QTest::newRow("unparseable tag")    << ""        << "0.2.2"              << false;
}

void TestVersionCompare::isNewer()
{
    QFETCH(QString, latestTag);
    QFETCH(QString, currentVersion);
    QFETCH(bool, expected);
    QCOMPARE(versioncompare::isNewer(latestTag, currentVersion), expected);
}

SMBMGR_TEST_MAIN(TestVersionCompare)

#include "tst_versioncompare.moc"
