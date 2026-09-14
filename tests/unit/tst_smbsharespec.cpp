#include <QtTest>

#include <QUrl>

#include "smb/SmbSession.h"

#include "common/TestMain.h"

class TestSmbShareSpec : public QObject
{
    Q_OBJECT

private slots:
    void acceptsBasicUrl();
    void acceptsPort();
    void acceptsDomainUser();
    void acceptsIpv6Host();
    void acceptsTrailingSlashOnShare();
    void rejects_data();
    void rejects();
    void hostPortForms();
};

void TestSmbShareSpec::acceptsBasicUrl()
{
    QString error;
    const auto spec = SmbShareSpec::fromUrl(QUrl("smb://dan@nas/media"), &error);
    QVERIFY(spec.has_value());
    QCOMPARE(spec->host, "nas");
    QCOMPARE(spec->port, -1);
    QCOMPARE(spec->user, "dan");
    QCOMPARE(spec->domain, QString());
    QCOMPARE(spec->share, "media");
    QCOMPARE(spec->displayName(), "dan@nas/media");
}

void TestSmbShareSpec::acceptsPort()
{
    QString error;
    const auto spec = SmbShareSpec::fromUrl(QUrl("smb://dan@nas:10445/media"), &error);
    QVERIFY(spec.has_value());
    QCOMPARE(spec->port, 10445);
    QCOMPARE(spec->hostPort(), "nas:10445");
}

void TestSmbShareSpec::acceptsDomainUser()
{
    QString error;
    const auto spec = SmbShareSpec::fromUrl(QUrl("smb://WORKGROUP;dan@nas/media"), &error);
    QVERIFY(spec.has_value());
    QCOMPARE(spec->domain, "WORKGROUP");
    QCOMPARE(spec->user, "dan");
}

void TestSmbShareSpec::acceptsIpv6Host()
{
    QString error;
    const auto spec = SmbShareSpec::fromUrl(QUrl("smb://dan@[::1]:10445/media"), &error);
    QVERIFY(spec.has_value());
    QCOMPARE(spec->host, "::1");
    // The bracketed form is what libsmb2 expects for a v6 address.
    QCOMPARE(spec->hostPort(), "[::1]:10445");
}

void TestSmbShareSpec::acceptsTrailingSlashOnShare()
{
    QString error;
    const auto spec = SmbShareSpec::fromUrl(QUrl("smb://dan@nas/media/"), &error);
    QVERIFY(spec.has_value());
    QCOMPARE(spec->share, "media");
}

void TestSmbShareSpec::rejects_data()
{
    QTest::addColumn<QString>("url");

    QTest::newRow("wrong scheme")        << "http://dan@nas/media";
    QTest::newRow("no scheme")           << "nas/media";
    QTest::newRow("missing host")        << "smb:///media";
    QTest::newRow("missing user")        << "smb://nas/media";
    QTest::newRow("empty user w/domain") << "smb://WORKGROUP;@nas/media";
    QTest::newRow("missing share")       << "smb://dan@nas";
    QTest::newRow("slash only share")    << "smb://dan@nas/";
    QTest::newRow("path after share")    << "smb://dan@nas/media/sub";
    QTest::newRow("bad port")            << "smb://dan@nas:notaport/media";
}

void TestSmbShareSpec::rejects()
{
    QFETCH(QString, url);
    QString error;
    const auto spec = SmbShareSpec::fromUrl(QUrl(url), &error);
    QVERIFY(!spec.has_value());
    // Every rejection must explain itself (it lands in the status bar).
    QVERIFY(!error.isEmpty());
}

void TestSmbShareSpec::hostPortForms()
{
    SmbShareSpec spec;
    spec.host = "nas";
    QCOMPARE(spec.hostPort(), "nas");
    spec.port = 445;
    QCOMPARE(spec.hostPort(), "nas:445");
    spec.host = "fe80::1";
    QCOMPARE(spec.hostPort(), "[fe80::1]:445");
    spec.port = -1;
    QCOMPARE(spec.hostPort(), "[fe80::1]");
}

SMBMGR_TEST_MAIN(TestSmbShareSpec)

#include "tst_smbsharespec.moc"
