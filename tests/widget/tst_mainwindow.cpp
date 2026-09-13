#include <QtTest>

#include <QLineEdit>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>

#include "ui/FileBrowserView.h"
#include "ui/MainWindow.h"
#include "ui/MatchFinderPanel.h"

#include "common/SmbFixture.h"
#include "common/TestMain.h"

// The main-window connect flow and toolbar logic, driven through the real
// widgets with a stubbed password prompt (the modal QInputDialog never runs
// under test).
class TestMainWindow : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void badUrlShowsErrorWithoutPrompt();
    void emptyUrlAsksForOne();
    void cancelledPromptStaysDisconnected();
    void connectFlowBuildsTwoViewsAndPanel();
    void defaultSplitIsEven();
    void disconnectRestoresPlaceholder();
    void rememberedUrl();

private:
    // Installs a prompt stub and drives the toolbar through a full connect.
    void connectWindow(MainWindow &window);

    SmbFixture m_fx;
};

void TestMainWindow::initTestCase()
{
    SmbSession session;
    HLM_CONNECT_OR_SKIP(m_fx, session);
}

void TestMainWindow::init()
{
    QSettings().clear(); // isolated by QStandardPaths test mode (TestMain.h)
}

void TestMainWindow::connectWindow(MainWindow &window)
{
    window.setPasswordPrompt(
        [this](const QString &) { return std::optional<QString>(m_fx.password()); });
    auto *urlEdit = window.findChild<QLineEdit *>("mw.urlEdit");
    auto *connectAction = window.findChild<QAction *>("mw.connectAction");
    QVERIFY(urlEdit && connectAction);
    urlEdit->setText(m_fx.url());
    connectAction->trigger();
    QTRY_COMPARE_WITH_TIMEOUT(connectAction->text(), "Disconnect", 15000);
}

void TestMainWindow::badUrlShowsErrorWithoutPrompt()
{
    MainWindow window;
    bool promptCalled = false;
    window.setPasswordPrompt([&promptCalled](const QString &) {
        promptCalled = true;
        return std::optional<QString>();
    });
    auto *urlEdit = window.findChild<QLineEdit *>("mw.urlEdit");
    auto *connectAction = window.findChild<QAction *>("mw.connectAction");

    urlEdit->setText("smb://host-without-user/share");
    connectAction->trigger();
    QVERIFY(!promptCalled);
    QVERIFY(window.statusBar()->currentMessage().contains("missing a user"));
    QCOMPARE(connectAction->text(), "Connect");

    urlEdit->setText("smb://user@host-without-share");
    connectAction->trigger();
    QVERIFY(!promptCalled);
    QVERIFY(window.statusBar()->currentMessage().contains("missing a share"));
}

void TestMainWindow::emptyUrlAsksForOne()
{
    MainWindow window;
    auto *connectAction = window.findChild<QAction *>("mw.connectAction");
    window.findChild<QLineEdit *>("mw.urlEdit")->clear();
    connectAction->trigger();
    QVERIFY(window.statusBar()->currentMessage().contains("Enter a share URL"));
}

void TestMainWindow::cancelledPromptStaysDisconnected()
{
    MainWindow window;
    window.setPasswordPrompt(
        [](const QString &) { return std::optional<QString>(); }); // = cancel
    auto *urlEdit = window.findChild<QLineEdit *>("mw.urlEdit");
    auto *connectAction = window.findChild<QAction *>("mw.connectAction");

    urlEdit->setText(m_fx.url());
    connectAction->trigger();

    QCOMPARE(connectAction->text(), "Connect"); // no attempt started
    QVERIFY(urlEdit->isEnabled());
    QCOMPARE(window.findChildren<FileBrowserView *>().size(), 0);
}

void TestMainWindow::connectFlowBuildsTwoViewsAndPanel()
{
    MainWindow window;
    connectWindow(window);

    // Exactly two views (ADR 0003) right of the Match Finder panel.
    QCOMPARE(window.findChildren<FileBrowserView *>().size(), 2);
    QCOMPARE(window.findChildren<MatchFinderPanel *>().size(), 1);
    QVERIFY(qobject_cast<QSplitter *>(window.centralWidget()));
    QVERIFY(!window.findChild<QLineEdit *>("mw.urlEdit")->isEnabled());
    QVERIFY(window.statusBar()->currentMessage().startsWith("Connected to"));

    // Both views come up listing the share root.
    for (FileBrowserView *view : window.findChildren<FileBrowserView *>()) {
        QTRY_COMPARE(view->currentPath(), "/");
    }
}

// With no saved splitter state, the panel and the views get half the width
// each.
void TestMainWindow::defaultSplitIsEven()
{
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    connectWindow(window);

    auto *splitter = qobject_cast<QSplitter *>(window.centralWidget());
    QVERIFY(splitter);
    QCOMPARE(splitter->count(), 2);
    // Sizes settle once the new central widget is laid out.
    QTRY_VERIFY2(qAbs(splitter->sizes().at(0) - splitter->sizes().at(1)) <= 1,
                 qPrintable(QStringLiteral("%1 vs %2")
                                .arg(splitter->sizes().at(0))
                                .arg(splitter->sizes().at(1))));
    QVERIFY(splitter->sizes().at(0) > window.width() / 4);
}

void TestMainWindow::disconnectRestoresPlaceholder()
{
    MainWindow window;
    connectWindow(window);
    auto *connectAction = window.findChild<QAction *>("mw.connectAction");

    connectAction->trigger(); // now "Disconnect"

    QCOMPARE(connectAction->text(), "Connect");
    QVERIFY(window.findChild<QLineEdit *>("mw.urlEdit")->isEnabled());
    QVERIFY(!qobject_cast<QSplitter *>(window.centralWidget()));
    // The old splitter (and the views under it) go away via deleteLater.
    QTRY_COMPARE(window.findChildren<FileBrowserView *>().size(), 0);
}

void TestMainWindow::rememberedUrl()
{
    {
        MainWindow window;
        connectWindow(window); // a successful connect stores connect/lastUrl
    }
    MainWindow relaunched;
    QCOMPARE(relaunched.findChild<QLineEdit *>("mw.urlEdit")->text(), m_fx.url());
}

HLM_TEST_MAIN(TestMainWindow)

#include "tst_mainwindow.moc"
