#include <QtTest>

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "core/LinkRunner.h"
#include "core/Logger.h"
#include "smb/SmbSession.h"

#include "common/SmbFixture.h"
#include "common/TestMain.h"

// The rename → link → unlink engine, against the Samba fixture, with every
// outcome verified out-of-band in the container. The Match Finder UI on top is
// covered by tst_matchfinderpanel.
class TestLinkRunner : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void happyPathSingleVictim();
    void multipleVictimsSequential();
    void alreadyLinkedPair();
    void linkFailureRestoresOriginal();
    void renameFailureLeavesVictimUntouched();
    void disconnectMidRun();

private:
    // Runs the jobs to completion, returning the collected signal payloads.
    struct RunResult
    {
        QList<QString> statuses;                    // jobStatusChanged texts
        QList<std::tuple<int, bool, QString>> jobs; // jobFinished args
        int succeeded = -1;
        int failed = -1;
    };
    RunResult run(SmbSession &session, const QList<LinkRunner::Job> &jobs);

    SmbFixture m_fx;
    QTemporaryDir m_logDir;
};

void TestLinkRunner::initTestCase()
{
    SmbSession session;
    SMBMGR_CONNECT_OR_SKIP(m_fx, session);
}

TestLinkRunner::RunResult TestLinkRunner::run(SmbSession &session,
                                              const QList<LinkRunner::Job> &jobs)
{
    LinkRunner runner(&session);
    RunResult result;
    connect(&runner, &LinkRunner::jobStatusChanged, this,
            [&result](int, const QString &text) { result.statuses.append(text); });
    connect(&runner, &LinkRunner::jobFinished, this,
            [&result](int index, bool success, const QString &message) {
                result.jobs.append({index, success, message});
            });
    connect(&runner, &LinkRunner::allFinished, this,
            [&result](int succeeded, int failed) {
                result.succeeded = succeeded;
                result.failed = failed;
            });

    runner.start(jobs);
    // allFinished sets the counts, so they mark completion. (QTRY_* returns on
    // failure, hence the void lambda inside this value-returning function.)
    [&] { QTRY_VERIFY_WITH_TIMEOUT(result.succeeded >= 0, 30000); }();
    return result;
}

void TestLinkRunner::happyPathSingleVictim()
{
    const QString dir = m_fx.makeCaseDir("happy");
    m_fx.seedFile(dir, "primary.bin", 100, 'p');
    m_fx.seedFile(dir, "victim.bin", 90, 'v');

    // A successful run must leave a complete audit trail: capture this test's
    // log lines in a file of their own.
    const QString logPath = m_logDir.filePath("happy.jsonl");
    Logger::instance().setFilePath(logPath);

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));
    const RunResult result =
        run(session, {{dir + "/primary.bin", dir + "/victim.bin"}});

    QCOMPARE(result.succeeded, 1);
    QCOMPARE(result.failed, 0);
    QCOMPARE(result.jobs.size(), 1);
    QVERIFY(std::get<1>(result.jobs.first()));
    QCOMPARE(std::get<2>(result.jobs.first()), "replaced with hard link");
    // The status column walks all three steps.
    QCOMPARE(result.statuses.size(), 3);

    QCOMPARE(m_fx.statPath(dir + "/primary.bin").nlink, 2);
    QCOMPARE(m_fx.statPath(dir + "/victim.bin").inode,
             m_fx.statPath(dir + "/primary.bin").inode);
    // The victim's content is now the primary's.
    QCOMPARE(m_fx.readFile(dir + "/victim.bin"), QString(100, QLatin1Char('p')));
    // No leftover *.smbmgr-tmp.
    QCOMPARE(m_fx.ls(dir).filter("smbmgr-tmp").size(), 0);

    // The audit trail holds the three server writes, in order, as info lines
    // with the operations' paths (plus the connect lines around them).
    Logger::instance().setFilePath(QString());
    QFile logFile(logPath);
    QVERIFY(logFile.open(QIODevice::ReadOnly));
    QList<QJsonObject> writes;
    for (const QByteArray &line : logFile.readAll().split('\n')) {
        if (line.isEmpty()) {
            continue;
        }
        const QJsonObject entry = QJsonDocument::fromJson(line).object();
        QVERIFY2(!entry.isEmpty(), line.constData());
        const QString msg = entry.value("msg").toString();
        if (msg == "file renamed" || msg == "hard link created"
            || msg == "file removed") {
            QCOMPARE(entry.value("level").toString(), "info");
            writes.append(entry);
        }
    }
    QCOMPARE(writes.size(), 3);
    QCOMPARE(writes[0].value("msg").toString(), "file renamed");
    QCOMPARE(writes[0].value("from").toString(), dir + "/victim.bin");
    QCOMPARE(writes[1].value("msg").toString(), "hard link created");
    QCOMPARE(writes[1].value("existing").toString(), dir + "/primary.bin");
    QCOMPARE(writes[1].value("new").toString(), dir + "/victim.bin");
    QCOMPARE(writes[2].value("msg").toString(), "file removed");
    QCOMPARE(writes[2].value("path").toString(), writes[0].value("to").toString());
}

void TestLinkRunner::multipleVictimsSequential()
{
    const QString dir = m_fx.makeCaseDir("threefiles");
    m_fx.seedFile(dir, "keep.bin", 64, 'k');
    m_fx.seedFile(dir, "v1.bin", 64);
    m_fx.seedFile(dir, "v2.bin", 64);

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));
    const RunResult result = run(session, {{dir + "/keep.bin", dir + "/v1.bin"},
                                           {dir + "/keep.bin", dir + "/v2.bin"}});

    QCOMPARE(result.succeeded, 2);
    QCOMPARE(result.failed, 0);
    QCOMPARE(m_fx.statPath(dir + "/keep.bin").nlink, 3);
    QCOMPARE(m_fx.statPath(dir + "/v1.bin").inode, m_fx.statPath(dir + "/keep.bin").inode);
    QCOMPARE(m_fx.statPath(dir + "/v2.bin").inode, m_fx.statPath(dir + "/keep.bin").inode);
    QCOMPARE(m_fx.ls(dir).filter("smbmgr-tmp").size(), 0);
}

// Linking two names that are already hard links of each other must lose no
// data.
void TestLinkRunner::alreadyLinkedPair()
{
    const QString dir = m_fx.makeCaseDir("alreadylinked");
    m_fx.seedFile(dir, "a.bin", 80, 'a');
    m_fx.makeHardLink(dir + "/a.bin", dir + "/b.bin");

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));
    const RunResult result = run(session, {{dir + "/a.bin", dir + "/b.bin"}});

    QCOMPARE(result.succeeded, 1);
    QCOMPARE(m_fx.statPath(dir + "/a.bin").nlink, 2);
    QCOMPARE(m_fx.statPath(dir + "/b.bin").inode, m_fx.statPath(dir + "/a.bin").inode);
    QCOMPARE(m_fx.readFile(dir + "/b.bin"), QString(80, QLatin1Char('a')));
    QCOMPARE(m_fx.ls(dir).filter("smbmgr-tmp").size(), 0);
}

// A nonexistent primary lets the rename succeed and the link step fail: the
// victim must come back under its original name with its original content.
void TestLinkRunner::linkFailureRestoresOriginal()
{
    const QString dir = m_fx.makeCaseDir("linkfail");
    m_fx.seedFile(dir, "victim.bin", 70, 'v');

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));
    const RunResult result =
        run(session, {{dir + "/gone.bin", dir + "/victim.bin"}});

    QCOMPARE(result.succeeded, 0);
    QCOMPARE(result.failed, 1);
    QVERIFY(!std::get<1>(result.jobs.first()));
    const QString message = std::get<2>(result.jobs.first());
    QVERIFY2(message.contains("link failed"), qPrintable(message));
    QVERIFY2(message.contains("(original restored)"), qPrintable(message));

    QVERIFY(m_fx.exists(dir + "/victim.bin"));
    QCOMPARE(m_fx.readFile(dir + "/victim.bin"), QString(70, QLatin1Char('v')));
    QCOMPARE(m_fx.statPath(dir + "/victim.bin").nlink, 1);
    QCOMPARE(m_fx.ls(dir).filter("smbmgr-tmp").size(), 0);
}

// If even the first step (rename) fails, the victim is untouched.
void TestLinkRunner::renameFailureLeavesVictimUntouched()
{
    const QString dir = m_fx.makeCaseDir("renamefail");
    m_fx.seedFile(dir, "primary.bin", 60, 'p');
    m_fx.seedFile(dir, "victim.bin", 60, 'v');
    m_fx.chmodPath(dir, "555"); // no write permission on the directory

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));
    const RunResult result =
        run(session, {{dir + "/primary.bin", dir + "/victim.bin"}});
    m_fx.chmodPath(dir, "775");

    QCOMPARE(result.failed, 1);
    QVERIFY2(std::get<2>(result.jobs.first()).startsWith("failed:"),
             qPrintable(std::get<2>(result.jobs.first())));
    QCOMPARE(m_fx.readFile(dir + "/victim.bin"), QString(60, QLatin1Char('v')));
    QCOMPARE(m_fx.statPath(dir + "/victim.bin").nlink, 1);
}

// Disconnect after the first job: the remaining jobs fail cleanly, no crash.
// (SmbSession queues op completions, so disconnecting from inside a
// jobFinished handler is the supported path.)
void TestLinkRunner::disconnectMidRun()
{
    const QString dir = m_fx.makeCaseDir("middisconnect");
    m_fx.seedFile(dir, "keep.bin", 50, 'k');
    m_fx.seedFile(dir, "v1.bin", 50);
    m_fx.seedFile(dir, "v2.bin", 50);
    m_fx.seedFile(dir, "v3.bin", 50);

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));

    LinkRunner runner(&session);
    int finishedJobs = 0;
    int succeeded = -1;
    int failed = -1;
    connect(&runner, &LinkRunner::jobFinished, this,
            [&](int, bool, const QString &) {
                if (++finishedJobs == 1) {
                    session.disconnectFromShare();
                }
            });
    connect(&runner, &LinkRunner::allFinished, this, [&](int ok, int bad) {
        succeeded = ok;
        failed = bad;
    });

    runner.start({{dir + "/keep.bin", dir + "/v1.bin"},
                  {dir + "/keep.bin", dir + "/v2.bin"},
                  {dir + "/keep.bin", dir + "/v3.bin"}});
    QTRY_VERIFY_WITH_TIMEOUT(succeeded >= 0, 30000);

    QCOMPARE(succeeded, 1);
    QCOMPARE(failed, 2);
    QCOMPARE(session.state(), SmbSession::State::Disconnected);
    // Job 1 completed fully before the disconnect.
    QCOMPARE(m_fx.statPath(dir + "/v1.bin").inode, m_fx.statPath(dir + "/keep.bin").inode);
    // The later victims were never touched (their renames failed up front).
    QCOMPARE(m_fx.statPath(dir + "/v2.bin").nlink, 1);
    QCOMPARE(m_fx.statPath(dir + "/v3.bin").nlink, 1);
}

SMBMGR_TEST_MAIN(TestLinkRunner)

#include "tst_linkrunner.moc"
