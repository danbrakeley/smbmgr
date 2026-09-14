#include <QtTest>

#include <QUrl>

#include "smb/SmbSession.h"

#include "common/SmbFixture.h"
#include "common/TestMain.h"

class TestSmbSession : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void connectAndDisconnect();
    void wrongPassword();
    void listsSeededDirectory();
    void listNonexistentFails();
    void statReportsLinkCountAndInode();
    void mutatingOperations();
    void immediateFailureCompletesAsync();
    void disconnectWithStatsInFlight();

private:
    SmbFixture m_fx;
};

// One reachability probe for the whole suite: QSKIP here skips every test.
void TestSmbSession::initTestCase()
{
    SmbSession session;
    SMBMGR_CONNECT_OR_SKIP(m_fx, session);
}

void TestSmbSession::connectAndDisconnect()
{
    SmbSession session;
    QSignalSpy stateSpy(&session, &SmbSession::stateChanged);
    QSignalSpy errorSpy(&session, &SmbSession::errorOccurred);

    QVERIFY(m_fx.connectForTest(session));
    QCOMPARE(qvariant_cast<SmbSession::State>(stateSpy.first().at(0)),
             SmbSession::State::Connecting);
    QCOMPARE(qvariant_cast<SmbSession::State>(stateSpy.last().at(0)),
             SmbSession::State::Connected);
    QVERIFY(errorSpy.isEmpty());

    session.disconnectFromShare();
    QCOMPARE(session.state(), SmbSession::State::Disconnected);
    QVERIFY(errorSpy.isEmpty());
}

// A wrong password returns to Disconnected with an authentication error.
void TestSmbSession::wrongPassword()
{
    SmbSession session;
    QSignalSpy errorSpy(&session, &SmbSession::errorOccurred);

    QString error;
    const auto spec = SmbShareSpec::fromUrl(QUrl(m_fx.url()), &error);
    QVERIFY(spec.has_value());
    session.connectToShare(*spec, "definitely-not-the-password");

    QTRY_COMPARE_WITH_TIMEOUT(session.state(), SmbSession::State::Disconnected, 15000);
    QTRY_VERIFY(errorSpy.count() >= 1);
    QVERIFY(!errorSpy.first().at(0).toString().isEmpty());
}

// Names, sizes, dir flags, and inodes come from enumeration; nlink always
// starts unknown (SMB2 enumeration never carries it).
void TestSmbSession::listsSeededDirectory()
{
    const QString dir = m_fx.makeCaseDir("list");
    m_fx.seedFile(dir, "one.bin", 100);
    m_fx.seedFile(dir, "two.bin", 200);
    m_fx.makeDir(dir + "/sub");

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));
    QSignalSpy listedSpy(&session, &SmbSession::directoryListed);

    session.listDirectory(dir);
    QTRY_COMPARE(listedSpy.count(), 1);
    QCOMPARE(listedSpy.first().at(0).toString(), dir);

    const auto entries = listedSpy.first().at(1).value<QList<FileEntry>>();
    QCOMPARE(entries.size(), 3);
    QHash<QString, FileEntry> byName;
    for (const FileEntry &entry : entries) {
        byName.insert(entry.name, entry);
    }
    QVERIFY(byName.contains("one.bin"));
    QVERIFY(byName.contains("two.bin"));
    QVERIFY(byName.contains("sub"));
    QCOMPARE(byName["one.bin"].size, 100ULL);
    QCOMPARE(byName["two.bin"].size, 200ULL);
    QVERIFY(!byName["one.bin"].isDir);
    QVERIFY(byName["sub"].isDir);
    QCOMPARE(byName["one.bin"].nlink, FileEntry::kNlinkUnknown);
    QVERIFY(byName["one.bin"].inode != 0);

    // Enumeration inode fidelity: must agree with the filesystem inside the
    // container (MatchSearcher's already-linked suppression depends on it).
    QCOMPARE(byName["one.bin"].inode, m_fx.statPath(dir + "/one.bin").inode);
}

// A failed listing echoes the requested path.
void TestSmbSession::listNonexistentFails()
{
    const QString dir = m_fx.makeCaseDir("badpath");

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));
    QSignalSpy failedSpy(&session, &SmbSession::directoryListFailed);

    session.listDirectory(dir + "/does-not-exist");
    QTRY_COMPARE(failedSpy.count(), 1);
    QCOMPARE(failedSpy.first().at(0).toString(), dir + "/does-not-exist");
    QVERIFY(!failedSpy.first().at(1).toString().isEmpty());
}

// statFile is the source of nlink, and its inode agrees with the container
// filesystem.
void TestSmbSession::statReportsLinkCountAndInode()
{
    const QString dir = m_fx.makeCaseDir("stat");
    m_fx.seedFile(dir, "linked_a.bin", 64);
    m_fx.makeHardLink(dir + "/linked_a.bin", dir + "/linked_b.bin");
    m_fx.seedFile(dir, "single.bin", 64);

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));
    QSignalSpy statSpy(&session, &SmbSession::fileStatted);

    session.statFile(dir + "/linked_a.bin");
    session.statFile(dir + "/linked_b.bin");
    session.statFile(dir + "/single.bin");
    QTRY_COMPARE(statSpy.count(), 3);

    QHash<QString, QPair<int, quint64>> results; // path -> (nlink, inode)
    for (const auto &args : statSpy) {
        results.insert(args.at(0).toString(),
                       {args.at(1).toInt(), args.at(2).toULongLong()});
    }
    QCOMPARE(results[dir + "/linked_a.bin"].first, 2);
    QCOMPARE(results[dir + "/linked_b.bin"].first, 2);
    QCOMPARE(results[dir + "/single.bin"].first, 1);
    QCOMPARE(results[dir + "/linked_a.bin"].second,
             results[dir + "/linked_b.bin"].second);
    QCOMPARE(results[dir + "/linked_a.bin"].second,
             m_fx.statPath(dir + "/linked_a.bin").inode);
    QVERIFY(results[dir + "/single.bin"].second
            != results[dir + "/linked_a.bin"].second);
}

// rename / link / unlink, each verified out-of-band in the container.
void TestSmbSession::mutatingOperations()
{
    const QString dir = m_fx.makeCaseDir("ops");
    m_fx.seedFile(dir, "orig.bin", 128, 'x');

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));
    QSignalSpy okSpy(&session, &SmbSession::operationSucceeded);
    QSignalSpy failSpy(&session, &SmbSession::operationFailed);

    const quint64 renameId = session.renameFile(dir + "/orig.bin", dir + "/renamed.bin");
    QTRY_COMPARE(okSpy.count(), 1);
    QCOMPARE(okSpy.last().at(0).toULongLong(), renameId);
    QVERIFY(!m_fx.exists(dir + "/orig.bin"));
    QVERIFY(m_fx.exists(dir + "/renamed.bin"));

    const quint64 linkId = session.createHardLink(dir + "/renamed.bin", dir + "/link.bin");
    QTRY_COMPARE(okSpy.count(), 2);
    QCOMPARE(okSpy.last().at(0).toULongLong(), linkId);
    QCOMPARE(m_fx.statPath(dir + "/renamed.bin").nlink, 2);
    QCOMPARE(m_fx.statPath(dir + "/link.bin").inode,
             m_fx.statPath(dir + "/renamed.bin").inode);

    const quint64 removeId = session.removeFile(dir + "/renamed.bin");
    QTRY_COMPARE(okSpy.count(), 3);
    QCOMPARE(okSpy.last().at(0).toULongLong(), removeId);
    QVERIFY(!m_fx.exists(dir + "/renamed.bin"));
    QCOMPARE(m_fx.statPath(dir + "/link.bin").nlink, 1);

    // The survivor still carries the original bytes.
    QCOMPARE(m_fx.readFile(dir + "/link.bin"), QString(128, QLatin1Char('x')));
    QVERIFY(failSpy.isEmpty());
}

// The completion contract: even an operation that fails immediately (no
// connection) must report asynchronously, never from inside the call.
void TestSmbSession::immediateFailureCompletesAsync()
{
    SmbSession session; // never connected
    QSignalSpy failSpy(&session, &SmbSession::operationFailed);

    const quint64 id = session.renameFile("/a.bin", "/b.bin");
    QVERIFY(failSpy.isEmpty()); // nothing synchronous

    QTRY_COMPARE(failSpy.count(), 1);
    QCOMPARE(failSpy.first().at(0).toULongLong(), id);
    QVERIFY(!failSpy.first().at(1).toString().isEmpty());
}

// Regression for the smb2_destroy_context teardown gotcha (SMB2_STATUS_
// SHUTDOWN flushes pending callbacks): disconnecting with a pipeline of
// throttled stats in flight must not crash, and the session must reconnect.
void TestSmbSession::disconnectWithStatsInFlight()
{
    const QString dir = m_fx.makeCaseDir("midfill");
    m_fx.seedManyFiles(dir, 40, 32);

    qputenv("SMBMGR_STAT_DELAY_MS", "100"); // read once in the ctor below
    SmbSession session;
    qunsetenv("SMBMGR_STAT_DELAY_MS");

    QVERIFY(m_fx.connectForTest(session));
    for (int i = 1; i <= 40; ++i) {
        session.statFile(dir + QStringLiteral("/f%1.bin").arg(i));
    }
    QTest::qWait(150); // some sent, most still queued behind the throttle

    session.disconnectFromShare();
    QCOMPARE(session.state(), SmbSession::State::Disconnected);
    QTest::qWait(250); // any straggling queued failures must not crash

    QVERIFY(m_fx.connectForTest(session));
    QSignalSpy listedSpy(&session, &SmbSession::directoryListed);
    session.listDirectory(dir);
    QTRY_COMPARE(listedSpy.count(), 1);
}

SMBMGR_TEST_MAIN(TestSmbSession)

#include "tst_smbsession.moc"
