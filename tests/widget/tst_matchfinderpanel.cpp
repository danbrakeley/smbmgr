#include <QtTest>

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTreeView>

#include "models/MatchResultsModel.h"
#include "smb/SmbSession.h"
#include "ui/CheckBoxHeader.h"
#include "ui/MatchFinderPanel.h"

#include "common/SmbFixture.h"
#include "common/TestMain.h"
#include "common/TestSupport.h"

// The Match Finder panel driven through its widgets. The search/pairing engine
// has its own suites.
class TestMatchFinderPanel : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void basicSearchShowsResults();
    void cancelRevertsButton();
    void checkAllAndLinkButtonEnablement();
    void conflictWarningBlocksRun();
    void linkRunWalksStatuses();
    void optionsPersist();
    void savedPathValidation();

private:
    void setSearchInputs(MatchFinderPanel &panel, const QString &primary,
                         const QString &secondary);
    static MatchResultsModel *resultsModel(MatchFinderPanel &panel);

    SmbFixture m_fx;
};

void TestMatchFinderPanel::initTestCase()
{
    SmbSession session;
    HLM_CONNECT_OR_SKIP(m_fx, session);
}

void TestMatchFinderPanel::init()
{
    QSettings().clear(); // options load in the ctor; start each test clean
}

void TestMatchFinderPanel::setSearchInputs(MatchFinderPanel &panel,
                                           const QString &primary,
                                           const QString &secondary)
{
    panel.findChild<QLineEdit *>("mfp.primaryPath")->setText(primary);
    panel.findChild<QLineEdit *>("mfp.secondaryPath")->setText(secondary);
    panel.findChild<QSpinBox *>("mfp.sizeMinValue")->setValue(0);
    panel.findChild<QComboBox *>("mfp.sizeMinUnit")->setCurrentIndex(0); // bytes
    panel.findChild<QSpinBox *>("mfp.sizeDiffValue")->setValue(0);
    panel.findChild<QComboBox *>("mfp.sizeDiffUnit")->setCurrentIndex(0);
}

MatchResultsModel *TestMatchFinderPanel::resultsModel(MatchFinderPanel &panel)
{
    return qobject_cast<MatchResultsModel *>(
        panel.findChild<QTreeView *>("mfp.tree")->model());
}

void TestMatchFinderPanel::basicSearchShowsResults()
{
    const QString dir = m_fx.makeCaseDir("panelbasic");
    m_fx.makeDir(dir + "/p");
    m_fx.makeDir(dir + "/s");
    m_fx.seedFile(dir + "/p", "a.bin", 1000);
    m_fx.seedFile(dir + "/s", "b.bin", 1000);

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));
    MatchFinderPanel panel(&session);
    panel.show();

    setSearchInputs(panel, dir + "/p", dir + "/s");
    panel.findChild<QPushButton *>("mfp.startButton")->click();

    QTRY_COMPARE(resultsModel(panel)->rowCount(), 1);
    QCOMPARE(resultsModel(panel)->matchAt(0).primaryPath, dir + "/p/a.bin");
    QTRY_VERIFY(panel.findChild<QLabel *>("mfp.statusLabel")
                    ->text().contains("1 potential match(es)."));
    QCOMPARE(panel.findChild<QPushButton *>("mfp.startButton")->text(),
             "Start Search");
}

void TestMatchFinderPanel::cancelRevertsButton()
{
    const QString dir = m_fx.makeCaseDir("panelcancel");
    m_fx.seedManyDirs(dir, 20, 64);

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));
    MatchFinderPanel panel(&session);
    panel.show();

    setSearchInputs(panel, dir, dir);
    auto *startButton = panel.findChild<QPushButton *>("mfp.startButton");
    startButton->click();
    // Everything is single-threaded: the search cannot progress between these
    // two clicks, so the cancel path is fully deterministic.
    QCOMPARE(startButton->text(), "Cancel Search");

    startButton->click(); // cancel
    QCOMPARE(startButton->text(), "Start Search");
    QCOMPARE(panel.findChild<QLabel *>("mfp.statusLabel")->text(),
             "Search cancelled.");

    // Late listing replies must not resurrect anything.
    QTest::qWait(500);
    QCOMPARE(startButton->text(), "Start Search");
    QCOMPARE(resultsModel(panel)->rowCount(), 0);
}

void TestMatchFinderPanel::checkAllAndLinkButtonEnablement()
{
    const QString dir = m_fx.makeCaseDir("panelcheckall");
    m_fx.makeDir(dir + "/p");
    m_fx.makeDir(dir + "/s");
    m_fx.seedFile(dir + "/p", "a.bin", 100);
    m_fx.seedFile(dir + "/s", "a2.bin", 100);
    m_fx.seedFile(dir + "/p", "b.bin", 200);
    m_fx.seedFile(dir + "/s", "b2.bin", 200);

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));
    MatchFinderPanel panel(&session);
    panel.show();

    setSearchInputs(panel, dir + "/p", dir + "/s");
    panel.findChild<QPushButton *>("mfp.startButton")->click();
    QTRY_COMPARE(resultsModel(panel)->rowCount(), 2);

    auto *linkButton = panel.findChild<QPushButton *>("mfp.linkButton");
    auto *model = resultsModel(panel);
    QVERIFY(!linkButton->isEnabled()); // nothing checked

    // Clicking the header checkbox checks every row; clicking again clears.
    auto *header = panel.findChild<CheckBoxHeader *>();
    QVERIFY(header);
    const QPoint inSection0(14, header->height() / 2);
    QTest::mouseClick(header->viewport(), Qt::LeftButton, {}, inSection0);
    QCOMPARE(model->checkedCount(), 2);
    QCOMPARE(model->aggregateCheckState(), Qt::Checked);
    QVERIFY(linkButton->isEnabled());

    // A partial selection reports the tri-state.
    model->setData(model->index(0, MatchResultsModel::CheckColumn),
                   Qt::Unchecked, Qt::CheckStateRole);
    QCOMPARE(model->aggregateCheckState(), Qt::PartiallyChecked);
    QVERIFY(linkButton->isEnabled()); // still >= 1 checked

    QTest::mouseClick(header->viewport(), Qt::LeftButton, {}, inSection0);
    QCOMPARE(model->checkedCount(), 2); // partial -> check all

    QTest::mouseClick(header->viewport(), Qt::LeftButton, {}, inSection0);
    QCOMPARE(model->checkedCount(), 0); // checked -> uncheck all
    QVERIFY(!linkButton->isEnabled());
}

void TestMatchFinderPanel::conflictWarningBlocksRun()
{
    const QString dir = m_fx.makeCaseDir("panelconflict");
    m_fx.seedFile(dir, "a.bin", 100);
    m_fx.seedFile(dir, "b.bin", 100);
    m_fx.seedFile(dir, "c.bin", 100);

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));
    MatchFinderPanel panel(&session);
    panel.show();

    // primary == secondary over three equal-size files yields all three
    // unordered pairs; checking every row guarantees a shared file.
    setSearchInputs(panel, dir, dir);
    panel.findChild<QPushButton *>("mfp.startButton")->click();
    QTRY_COMPARE(resultsModel(panel)->rowCount(), 3);
    resultsModel(panel)->setAllChecked(true);

    testsupport::autoAnswerNextMessageBox(QMessageBox::Ok); // the warning
    panel.findChild<QPushButton *>("mfp.linkButton")->click();

    // The run was blocked: nothing changed on the share, rows stay editable.
    QTest::qWait(300);
    QCOMPARE(m_fx.statPath(dir + "/a.bin").nlink, 1);
    QCOMPARE(m_fx.statPath(dir + "/b.bin").nlink, 1);
    QCOMPARE(m_fx.statPath(dir + "/c.bin").nlink, 1);
    QVERIFY(resultsModel(panel)->flags(resultsModel(panel)->index(0, 0))
            & Qt::ItemIsUserCheckable); // not locked
}

void TestMatchFinderPanel::linkRunWalksStatuses()
{
    const QString dir = m_fx.makeCaseDir("panelrun");
    m_fx.makeDir(dir + "/p");
    m_fx.makeDir(dir + "/s");
    m_fx.seedFile(dir + "/p", "keep.bin", 500, 'k');
    m_fx.seedFile(dir + "/s", "victim.bin", 500, 'v');

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));
    MatchFinderPanel panel(&session);
    panel.show();
    QSignalSpy runFinishedSpy(&panel, &MatchFinderPanel::linkRunFinished);

    setSearchInputs(panel, dir + "/p", dir + "/s");
    panel.findChild<QPushButton *>("mfp.startButton")->click();
    QTRY_COMPARE(resultsModel(panel)->rowCount(), 1);

    resultsModel(panel)->setAllChecked(true);
    testsupport::autoAnswerNextMessageBox(QMessageBox::Yes); // "Replace 1 file(s)?"
    panel.findChild<QPushButton *>("mfp.linkButton")->click();

    QTRY_COMPARE_WITH_TIMEOUT(runFinishedSpy.count(), 1, 30000);
    QCOMPARE(resultsModel(panel)
                 ->index(0, MatchResultsModel::StatusColumn).data().toString(),
             "replaced with hard link");

    QCOMPARE(m_fx.statPath(dir + "/p/keep.bin").nlink, 2);
    QCOMPARE(m_fx.statPath(dir + "/s/victim.bin").inode,
             m_fx.statPath(dir + "/p/keep.bin").inode);
    QCOMPARE(m_fx.readFile(dir + "/s/victim.bin"), QString(500, QLatin1Char('k')));
    QCOMPARE(m_fx.ls(dir + "/s").filter("hlmgr-tmp").size(), 0);
}

void TestMatchFinderPanel::optionsPersist()
{
    SmbSession session; // never needs to connect for this one

    {
        MatchFinderPanel panel(&session);
        auto *primary = panel.findChild<QLineEdit *>("mfp.primaryPath");
        primary->setText("/media/tv");
        QTest::keyClick(primary, Qt::Key_Return); // editingFinished -> saved
        auto *secondary = panel.findChild<QLineEdit *>("mfp.secondaryPath");
        secondary->setText("/media/movies");
        QTest::keyClick(secondary, Qt::Key_Return);
        panel.findChild<QCheckBox *>("mfp.primaryRecurse")->setChecked(false);
        panel.findChild<QSpinBox *>("mfp.sizeMinValue")->setValue(25);
        panel.findChild<QComboBox *>("mfp.sizeMinUnit")->setCurrentIndex(1);  // KiB
        panel.findChild<QSpinBox *>("mfp.sizeDiffValue")->setValue(3);
        panel.findChild<QComboBox *>("mfp.sizeDiffUnit")->setCurrentIndex(3); // GiB
    }

    MatchFinderPanel reopened(&session);
    QCOMPARE(reopened.findChild<QLineEdit *>("mfp.primaryPath")->text(), "/media/tv");
    QCOMPARE(reopened.findChild<QLineEdit *>("mfp.secondaryPath")->text(), "/media/movies");
    QVERIFY(!reopened.findChild<QCheckBox *>("mfp.primaryRecurse")->isChecked());
    QVERIFY(reopened.findChild<QCheckBox *>("mfp.secondaryRecurse")->isChecked());
    QCOMPARE(reopened.findChild<QSpinBox *>("mfp.sizeMinValue")->value(), 25);
    QCOMPARE(reopened.findChild<QComboBox *>("mfp.sizeMinUnit")->currentIndex(), 1);
    QCOMPARE(reopened.findChild<QSpinBox *>("mfp.sizeDiffValue")->value(), 3);
    QCOMPARE(reopened.findChild<QComboBox *>("mfp.sizeDiffUnit")->currentIndex(), 3);
}

void TestMatchFinderPanel::savedPathValidation()
{
    const QString goodDir = m_fx.makeCaseDir("panelvalidate");

    QSettings().setValue("matchfinder/primaryPath", goodDir);
    QSettings().setValue("matchfinder/secondaryPath", "/no-such-dir-anywhere");

    SmbSession session;
    QVERIFY(m_fx.connectForTest(session));
    MatchFinderPanel panel(&session); // loads both saved paths
    QSignalSpy statusSpy(&panel, &MatchFinderPanel::statusMessage);

    panel.beginPathValidation();

    // The bad path is cleared (edit + setting) with an explanation; the valid
    // one survives untouched.
    QTRY_VERIFY(panel.findChild<QLineEdit *>("mfp.secondaryPath")->text().isEmpty());
    QCOMPARE(panel.findChild<QLineEdit *>("mfp.primaryPath")->text(), goodDir);
    QTRY_VERIFY(statusSpy.count() >= 1);
    QVERIFY(statusSpy.first().at(0).toString().contains("was not found"));
    QCOMPARE(QSettings().value("matchfinder/secondaryPath").toString(), QString());
    QCOMPARE(QSettings().value("matchfinder/primaryPath").toString(), goodDir);
}

HLM_TEST_MAIN(TestMatchFinderPanel)

#include "tst_matchfinderpanel.moc"
