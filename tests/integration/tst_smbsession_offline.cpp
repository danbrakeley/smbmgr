#include <QtTest>

#include "smb/SmbSession.h"

#include "common/TestMain.h"

// Connect-failure behavior that needs no server at all.
class TestSmbSessionOffline : public QObject
{
    Q_OBJECT

private slots:
    void unresolvableHost();
    void abortMidConnect();
};

// An unresolvable host fails on its own with a resolve error, without blocking
// the event loop.
void TestSmbSessionOffline::unresolvableHost()
{
    SmbSession session;
    QSignalSpy errorSpy(&session, &SmbSession::errorOccurred);

    SmbShareSpec spec;
    spec.host = "name.invalid"; // RFC 2606: guaranteed not to resolve
    spec.user = "user";
    spec.share = "share";
    session.connectToShare(spec, QString());
    QCOMPARE(session.state(), SmbSession::State::Connecting);

    QTRY_COMPARE_WITH_TIMEOUT(session.state(), SmbSession::State::Disconnected, 30000);
    QTRY_VERIFY(errorSpy.count() >= 1);
    QVERIFY(!errorSpy.first().at(0).toString().isEmpty());
}

// Aborting during a hanging TCP connect returns to Disconnected immediately.
void TestSmbSessionOffline::abortMidConnect()
{
    SmbSession session;

    SmbShareSpec spec;
    // TEST-NET-1 (RFC 5737) is unrouted on sane networks, so the TCP connect
    // hangs. If a network answers this address the test may pass trivially or
    // flake; keep assertions tolerant.
    spec.host = "192.0.2.1";
    spec.user = "user";
    spec.share = "share";
    session.connectToShare(spec, QString());
    QCOMPARE(session.state(), SmbSession::State::Connecting);

    QTest::qWait(250); // let the connect get properly in flight
    if (session.state() != SmbSession::State::Connecting) {
        QSKIP("192.0.2.1 answered or failed instantly on this network");
    }

    session.abortConnect();
    QCOMPARE(session.state(), SmbSession::State::Disconnected);

    // The session must remain usable: a fresh attempt starts cleanly.
    session.connectToShare(spec, QString());
    QCOMPARE(session.state(), SmbSession::State::Connecting);
    session.abortConnect();
    QCOMPARE(session.state(), SmbSession::State::Disconnected);
}

HLM_TEST_MAIN(TestSmbSessionOffline)

#include "tst_smbsession_offline.moc"
