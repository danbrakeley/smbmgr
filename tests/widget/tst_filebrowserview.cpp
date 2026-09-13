#include <QtTest>

#include <QAction>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QToolButton>
#include <QTreeView>

#include "models/FileListModel.h"
#include "smb/SmbSession.h"
#include "ui/FileBrowserView.h"

#include "common/SmbFixture.h"
#include "common/TestMain.h"

// One file-browser view against the Samba fixture: navigation, filter, status
// bar, and the lazy link-count fill-in (throttled via HLM_STAT_DELAY_MS to
// make it observable).
class TestFileBrowserView : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void listsDirectoryAndStatusBar();
    void navigationByActivationAndPathBox();
    void badPathReverts();
    void upButton();
    void viewIconModeMenu();
    void filterAndSelectionUpdateStatusBar();
    void statPumpFillsAndDrains();
    void navigationCancelsFill();
    void disconnectMidFill();

private:
    QString linksTextAt(QTreeView *tree, int proxyRow);

    SmbFixture m_fx;
};

void TestFileBrowserView::initTestCase()
{
    SmbSession session;
    HLM_CONNECT_OR_SKIP(m_fx, session);
}

QString TestFileBrowserView::linksTextAt(QTreeView *tree, int proxyRow)
{
    return tree->model()
        ->index(proxyRow, FileListModel::LinksColumn)
        .data()
        .toString();
}

void TestFileBrowserView::listsDirectoryAndStatusBar()
{
    const QString dir = m_fx.makeCaseDir("browse");
    m_fx.seedFile(dir, "one.bin", 100);
    m_fx.seedFile(dir, "two.bin", 200);
    m_fx.makeDir(dir + "/sub");

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));
    FileBrowserView view(&session);

    view.navigateTo(dir);
    QTRY_COMPARE(view.currentPath(), dir);

    auto *tree = view.findChild<QTreeView *>("fbv.tree");
    QCOMPARE(tree->model()->rowCount(), 3);
    // Folders group on top under the default name sort.
    QCOMPARE(tree->model()->index(0, FileListModel::NameColumn).data().toString(),
             "sub");
    QCOMPARE(view.findChild<QLabel *>("fbv.visibleLabel")->text(), "Visible: 3");
    QCOMPARE(view.findChild<QLabel *>("fbv.totalLabel")->text(), "Total: 3");
    QCOMPARE(view.findChild<QLabel *>("fbv.selectedLabel")->text(), "Selected: 0");
    QCOMPARE(view.findChild<QLineEdit *>("fbv.pathEdit")->text(), dir);
}

void TestFileBrowserView::navigationByActivationAndPathBox()
{
    const QString dir = m_fx.makeCaseDir("navigate");
    m_fx.makeDir(dir + "/sub");
    m_fx.seedFile(dir + "/sub", "inner.bin", 50);

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));
    FileBrowserView view(&session);

    view.navigateTo(dir);
    QTRY_COMPARE(view.currentPath(), dir);

    // Select the folder row and press Enter to navigate into it.
    auto *tree = view.findChild<QTreeView *>("fbv.tree");
    tree->setCurrentIndex(tree->model()->index(0, 0)); // "sub", folders on top
    QTest::keyClick(tree, Qt::Key_Return);
    QTRY_COMPARE(view.currentPath(), dir + "/sub");
    QTRY_COMPARE(tree->model()->rowCount(), 1);

    // Path box with a ".." segment normalizes and lists the parent.
    auto *pathEdit = view.findChild<QLineEdit *>("fbv.pathEdit");
    pathEdit->setText(dir + "/sub/..");
    QTest::keyClick(pathEdit, Qt::Key_Return);
    QTRY_COMPARE(view.currentPath(), dir);
}

void TestFileBrowserView::badPathReverts()
{
    const QString dir = m_fx.makeCaseDir("badpath");
    m_fx.seedFile(dir, "keep.bin", 10);

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));
    FileBrowserView view(&session);
    QSignalSpy errorSpy(&view, &FileBrowserView::errorOccurred);

    view.navigateTo(dir);
    QTRY_COMPARE(view.currentPath(), dir);

    auto *pathEdit = view.findChild<QLineEdit *>("fbv.pathEdit");
    pathEdit->setText(dir + "/no-such-dir");
    QTest::keyClick(pathEdit, Qt::Key_Return);

    QTRY_COMPARE(errorSpy.count(), 1);
    QCOMPARE(view.currentPath(), dir);   // previous listing stays
    QCOMPARE(pathEdit->text(), dir);     // the box reverts
    QCOMPARE(view.findChild<QTreeView *>("fbv.tree")->model()->rowCount(), 1);
}

void TestFileBrowserView::upButton()
{
    const QString dir = m_fx.makeCaseDir("upbutton");
    m_fx.makeDir(dir + "/sub");

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));
    FileBrowserView view(&session);
    auto *upButton = view.findChild<QToolButton *>("fbv.upButton");

    view.navigateTo("/");
    QTRY_COMPARE(view.currentPath(), "/");
    QVERIFY(!upButton->isEnabled()); // disabled at the root

    view.navigateTo(dir + "/sub");
    QTRY_COMPARE(view.currentPath(), dir + "/sub");
    QVERIFY(upButton->isEnabled());

    upButton->click();
    QTRY_COMPARE(view.currentPath(), dir); // immediate parent
}

void TestFileBrowserView::viewIconModeMenu()
{
    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));
    FileBrowserView view(&session);

    auto *osAction = view.findChild<QAction *>("fbv.viewIconsOs");
    auto *genericAction = view.findChild<QAction *>("fbv.viewIconsGeneric");
    QVERIFY(osAction);
    QVERIFY(genericAction);
    QVERIFY(osAction->isCheckable());
    QVERIFY(genericAction->isCheckable());
    QVERIFY(osAction->actionGroup()); // mutually exclusive with genericAction
    QCOMPARE(osAction->actionGroup(), genericAction->actionGroup());

    // Defaults to OS icons.
    QVERIFY(osAction->isChecked());
    QVERIFY(!genericAction->isChecked());

    view.setIconMode(FileListModel::IconMode::Generic);
    QVERIFY(!osAction->isChecked());
    QVERIFY(genericAction->isChecked());

    view.setIconMode(FileListModel::IconMode::Os);
    QVERIFY(osAction->isChecked());
    QVERIFY(!genericAction->isChecked());
}

void TestFileBrowserView::filterAndSelectionUpdateStatusBar()
{
    const QString dir = m_fx.makeCaseDir("filter");
    m_fx.seedFile(dir, "alpha.bin", 10);
    m_fx.seedFile(dir, "beta.bin", 10);
    m_fx.seedFile(dir, "BETA_copy.bin", 10);

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));
    FileBrowserView view(&session);
    view.navigateTo(dir);
    QTRY_COMPARE(view.currentPath(), dir);

    auto *filterEdit = view.findChild<QLineEdit *>("fbv.filterEdit");
    auto *selectedLabel = view.findChild<QLabel *>("fbv.selectedLabel");
    auto *visibleLabel = view.findChild<QLabel *>("fbv.visibleLabel");
    auto *totalLabel = view.findChild<QLabel *>("fbv.totalLabel");
    auto *tree = view.findChild<QTreeView *>("fbv.tree");

    QCOMPARE(visibleLabel->text(), "Visible: 3");
    QCOMPARE(totalLabel->text(), "Total: 3");

    filterEdit->setText("beta"); // case-insensitive substring
    QCOMPARE(tree->model()->rowCount(), 2);
    QCOMPARE(visibleLabel->text(), "Visible: 2");
    QCOMPARE(totalLabel->text(), "Total: 3");

    filterEdit->clear();
    QCOMPARE(tree->model()->rowCount(), 3);
    QCOMPARE(visibleLabel->text(), "Visible: 3");
    QCOMPARE(totalLabel->text(), "Total: 3");

    QCOMPARE(selectedLabel->text(), "Selected: 0");
    tree->setCurrentIndex(tree->model()->index(0, 0));
    QCOMPARE(selectedLabel->text(), "Selected: 1");

    // Navigating away resets the selection along with the listing.
    view.navigateTo(dir + "/..");
    QTRY_VERIFY(view.currentPath() != dir);
    QCOMPARE(selectedLabel->text(), "Selected: 0");
}

void TestFileBrowserView::statPumpFillsAndDrains()
{
    const QString dir = m_fx.makeCaseDir("statpump");
    m_fx.seedManyFiles(dir, 80, 32);
    m_fx.makeHardLink(dir + "/f1.bin", dir + "/f1_link.bin");

    qputenv("HLM_STAT_DELAY_MS", "50"); // read once in the session ctor
    SmbSession session;
    qunsetenv("HLM_STAT_DELAY_MS");
    QVERIFY(m_fx.connectForTest(session));

    FileBrowserView view(&session);
    view.navigateTo(dir);
    QTRY_COMPARE(view.currentPath(), dir);

    auto *tree = view.findChild<QTreeView *>("fbv.tree");
    const int rows = tree->model()->rowCount();
    QCOMPARE(rows, 81);

    // With the throttle on, the Links column starts as "…" placeholders.
    int pending = 0;
    for (int row = 0; row < rows; ++row) {
        if (linksTextAt(tree, row) == "…") {
            ++pending;
        }
    }
    QVERIFY(pending > 0);

    // ... and eventually every row drains to a real number.
    const QRegularExpression digits("^\\d+$");
    [&] {
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                for (int row = 0; row < rows; ++row) {
                    if (!digits.match(linksTextAt(tree, row)).hasMatch()) {
                        return false;
                    }
                }
                return true;
            }(),
            30000);
    }();

    // The hard-linked pair reports 2, everything else 1.
    QHash<QString, QString> linksByName;
    for (int row = 0; row < rows; ++row) {
        linksByName.insert(
            tree->model()->index(row, FileListModel::NameColumn).data().toString(),
            linksTextAt(tree, row));
    }
    QCOMPARE(linksByName.value("f1.bin"), "2");
    QCOMPARE(linksByName.value("f1_link.bin"), "2");
    QCOMPARE(linksByName.value("f2.bin"), "1");
}

void TestFileBrowserView::navigationCancelsFill()
{
    const QString big = m_fx.makeCaseDir("cancelfill_big");
    m_fx.seedManyFiles(big, 60, 16);
    const QString small = m_fx.makeCaseDir("cancelfill_small");
    m_fx.seedFile(small, "only.bin", 16);

    qputenv("HLM_STAT_DELAY_MS", "50");
    SmbSession session;
    qunsetenv("HLM_STAT_DELAY_MS");
    QVERIFY(m_fx.connectForTest(session));

    FileBrowserView view(&session);
    view.navigateTo(big);
    QTRY_COMPARE(view.currentPath(), big);

    // Navigate away while the fill is still in progress.
    view.navigateTo(small);
    QTRY_COMPARE(view.currentPath(), small);

    auto *tree = view.findChild<QTreeView *>("fbv.tree");
    QCOMPARE(tree->model()->rowCount(), 1);
    // The new directory fills normally; late replies for the old one are
    // dropped rather than landing in the wrong rows.
    QTRY_COMPARE_WITH_TIMEOUT(linksTextAt(tree, 0), QString("1"), 15000);
    QCOMPARE(tree->model()->index(0, FileListModel::NameColumn).data().toString(),
             "only.bin");
}

void TestFileBrowserView::disconnectMidFill()
{
    const QString dir = m_fx.makeCaseDir("disconnectfill");
    m_fx.seedManyFiles(dir, 60, 16);

    qputenv("HLM_STAT_DELAY_MS", "50");
    SmbSession session;
    qunsetenv("HLM_STAT_DELAY_MS");
    QVERIFY(m_fx.connectForTest(session));

    FileBrowserView view(&session);
    view.navigateTo(dir);
    QTRY_COMPARE(view.currentPath(), dir);

    session.disconnectFromShare(); // mid-fill; must not crash
    QCOMPARE(session.state(), SmbSession::State::Disconnected);
    QTest::qWait(300); // let any queued/straggling stat failures land

    // Reconnect and refresh: the view recovers fully.
    QVERIFY(m_fx.connectForTest(session));
    view.refresh();
    QTRY_COMPARE(view.currentPath(), dir);
    QTRY_COMPARE(view.findChild<QTreeView *>("fbv.tree")->model()->rowCount(), 60);
}

HLM_TEST_MAIN(TestFileBrowserView)

#include "tst_filebrowserview.moc"
