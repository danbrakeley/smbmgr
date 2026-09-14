#include <QtTest>

#include "models/MatchResultsModel.h"

#include "common/TestMain.h"

namespace {

QList<MatchSearcher::Match> sampleMatches()
{
    return {
        {"/p/one.bin", "/s/one.bin", 100, 100},
        {"/p/two.bin", "/s/two.bin", 200, 205},
        {"/p/three.bin", "/s/three.bin", 300, 300},
    };
}

QModelIndex checkIndex(const MatchResultsModel &model, int row)
{
    return model.index(row, MatchResultsModel::CheckColumn);
}

} // namespace

class TestMatchResultsModel : public QObject
{
    Q_OBJECT

private slots:
    void populates();
    void checkToggling();
    void checkedRowsInOrder();
    void setAllChecked();
    void aggregateCheckState();
    void lockedBlocksChanges();
    void setStatusUpdatesRow();
    void setMatchesClearsChecks();
};

void TestMatchResultsModel::populates()
{
    MatchResultsModel model;
    QSignalSpy spy(&model, &MatchResultsModel::checkedCountChanged);
    model.setMatches(sampleMatches());

    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.columnCount(), int(MatchResultsModel::ColumnCount));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toInt(), 0);

    // Display shows the file names; the full paths live in the tooltips.
    QCOMPARE(model.index(0, MatchResultsModel::PrimaryColumn).data().toString(),
             "one.bin");
    QCOMPARE(model.index(0, MatchResultsModel::PrimaryColumn)
                 .data(Qt::ToolTipRole).toString(),
             "/p/one.bin");
    QCOMPARE(model.index(0, MatchResultsModel::SecondaryColumn).data().toString(),
             "one.bin");
    QCOMPARE(checkIndex(model, 0).data(Qt::CheckStateRole).toInt(),
             int(Qt::Unchecked));
    QCOMPARE(model.matchAt(1).primaryPath, "/p/two.bin");
}

void TestMatchResultsModel::checkToggling()
{
    MatchResultsModel model;
    model.setMatches(sampleMatches());
    QSignalSpy spy(&model, &MatchResultsModel::checkedCountChanged);

    QVERIFY(model.setData(checkIndex(model, 1), Qt::Checked, Qt::CheckStateRole));
    QCOMPARE(model.checkedCount(), 1);
    QCOMPARE(spy.count(), 1);

    // Re-setting the same state is accepted but silent.
    QVERIFY(model.setData(checkIndex(model, 1), Qt::Checked, Qt::CheckStateRole));
    QCOMPARE(spy.count(), 1);

    QVERIFY(model.setData(checkIndex(model, 1), Qt::Unchecked, Qt::CheckStateRole));
    QCOMPARE(model.checkedCount(), 0);
    QCOMPARE(spy.count(), 2);
}

void TestMatchResultsModel::checkedRowsInOrder()
{
    MatchResultsModel model;
    model.setMatches(sampleMatches());
    model.setData(checkIndex(model, 2), Qt::Checked, Qt::CheckStateRole);
    model.setData(checkIndex(model, 0), Qt::Checked, Qt::CheckStateRole);
    QCOMPARE(model.checkedRows(), QList<int>({0, 2}));
}

void TestMatchResultsModel::setAllChecked()
{
    MatchResultsModel model;
    model.setMatches(sampleMatches());
    QSignalSpy spy(&model, &MatchResultsModel::checkedCountChanged);

    model.setAllChecked(true);
    QCOMPARE(model.checkedCount(), 3);
    QCOMPARE(spy.count(), 1);

    model.setAllChecked(true); // no change, no signal
    QCOMPARE(spy.count(), 1);

    model.setAllChecked(false);
    QCOMPARE(model.checkedCount(), 0);
    QCOMPARE(spy.count(), 2);
}

void TestMatchResultsModel::aggregateCheckState()
{
    MatchResultsModel model;
    QCOMPARE(model.aggregateCheckState(), Qt::Unchecked); // empty model

    model.setMatches(sampleMatches());
    QCOMPARE(model.aggregateCheckState(), Qt::Unchecked);

    model.setData(checkIndex(model, 0), Qt::Checked, Qt::CheckStateRole);
    QCOMPARE(model.aggregateCheckState(), Qt::PartiallyChecked);

    model.setAllChecked(true);
    QCOMPARE(model.aggregateCheckState(), Qt::Checked);
}

void TestMatchResultsModel::lockedBlocksChanges()
{
    MatchResultsModel model;
    model.setMatches(sampleMatches());
    model.setLocked(true);

    QVERIFY(!(model.flags(checkIndex(model, 0)) & Qt::ItemIsUserCheckable));
    QVERIFY(!model.setData(checkIndex(model, 0), Qt::Checked, Qt::CheckStateRole));
    model.setAllChecked(true);
    QCOMPARE(model.checkedCount(), 0);

    model.setLocked(false);
    QVERIFY(model.flags(checkIndex(model, 0)) & Qt::ItemIsUserCheckable);
    QVERIFY(model.setData(checkIndex(model, 0), Qt::Checked, Qt::CheckStateRole));
    QCOMPARE(model.checkedCount(), 1);
}

void TestMatchResultsModel::setStatusUpdatesRow()
{
    MatchResultsModel model;
    model.setMatches(sampleMatches());
    QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);

    model.setStatus(1, "renaming…");

    QCOMPARE(spy.count(), 1);
    const auto args = spy.takeFirst();
    QCOMPARE(args.at(0).toModelIndex().row(), 1);
    QCOMPARE(args.at(0).toModelIndex().column(), int(MatchResultsModel::StatusColumn));
    QCOMPARE(model.index(1, MatchResultsModel::StatusColumn).data().toString(),
             "renaming…");
}

void TestMatchResultsModel::setMatchesClearsChecks()
{
    MatchResultsModel model;
    model.setMatches(sampleMatches());
    model.setAllChecked(true);
    QCOMPARE(model.checkedCount(), 3);

    model.setMatches(sampleMatches());
    QCOMPARE(model.checkedCount(), 0);
    QCOMPARE(checkIndex(model, 0).data(Qt::CheckStateRole).toInt(),
             int(Qt::Unchecked));
}

SMBMGR_TEST_MAIN(TestMatchResultsModel)

#include "tst_matchresultsmodel.moc"
