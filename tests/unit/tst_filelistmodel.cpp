#include <QtTest>

#include <QDateTime>

#include "models/FileListModel.h"

#include "common/TestMain.h"

namespace {

QList<FileEntry> sampleEntries()
{
    const QDateTime when = QDateTime::currentDateTime();
    return {
        {"docs", true, 0, when, 100, FileEntry::kNlinkUnknown},
        {"a.txt", false, 1234, when, 101, FileEntry::kNlinkUnknown},
        {"b.txt", false, 5678, when, 102, 2},
        {"c.txt", false, 42, when, 103, FileEntry::kNlinkUnavailable},
    };
}

} // namespace

class TestFileListModel : public QObject
{
    Q_OBJECT

private slots:
    void counts();
    void displayValues();
    void sortRoleValues();
    void isDirRole();
    void setNlinkUpdatesRow();
    void setNlinkSameValueIsSilent();
    void setNlinkIgnoresBadRows();
    void setIconModeUpdatesIconRole();
    void setIconModeSameValueIsSilent();
};

void TestFileListModel::counts()
{
    FileListModel model;
    QCOMPARE(model.rowCount(), 0);
    model.setEntries(sampleEntries());
    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.columnCount(), int(FileListModel::ColumnCount));
    QCOMPARE(model.entryAt(1).name, "a.txt");
}

void TestFileListModel::displayValues()
{
    FileListModel model;
    model.setEntries(sampleEntries());

    auto display = [&](int row, int column) {
        return model.data(model.index(row, column), Qt::DisplayRole);
    };

    QCOMPARE(display(0, FileListModel::NameColumn).toString(), "docs");
    // Folders show no size and no link count.
    QVERIFY(!display(0, FileListModel::SizeColumn).isValid());
    QVERIFY(!display(0, FileListModel::LinksColumn).isValid());
    // Files show a formatted size and their nlink state.
    QVERIFY(!display(1, FileListModel::SizeColumn).toString().isEmpty());
    QCOMPARE(display(1, FileListModel::LinksColumn).toString(), "…");
    QCOMPARE(display(2, FileListModel::LinksColumn).toString(), "2");
    QCOMPARE(display(3, FileListModel::LinksColumn).toString(), "?");
    QCOMPARE(display(2, FileListModel::InodeColumn).toString(), "102");
}

void TestFileListModel::sortRoleValues()
{
    FileListModel model;
    model.setEntries(sampleEntries());

    auto sortValue = [&](int row, int column) {
        return model.data(model.index(row, column), FileListModel::SortRole);
    };

    QCOMPARE(sortValue(1, FileListModel::NameColumn).toString(), "a.txt");
    QCOMPARE(sortValue(1, FileListModel::SizeColumn).toULongLong(), 1234ULL);
    QCOMPARE(sortValue(1, FileListModel::ModifiedColumn).toDateTime(),
             model.entryAt(1).modified);
    QCOMPARE(sortValue(1, FileListModel::InodeColumn).toULongLong(), 101ULL);
    // "…" (unknown) and "?" (unavailable) sort below every real count.
    QVERIFY(sortValue(1, FileListModel::LinksColumn).toInt()
            < sortValue(2, FileListModel::LinksColumn).toInt());
    QVERIFY(sortValue(3, FileListModel::LinksColumn).toInt()
            < sortValue(2, FileListModel::LinksColumn).toInt());
}

void TestFileListModel::isDirRole()
{
    FileListModel model;
    model.setEntries(sampleEntries());
    QVERIFY(model.data(model.index(0, FileListModel::NameColumn),
                       FileListModel::IsDirRole).toBool());
    QVERIFY(!model.data(model.index(1, FileListModel::NameColumn),
                        FileListModel::IsDirRole).toBool());
}

void TestFileListModel::setNlinkUpdatesRow()
{
    FileListModel model;
    model.setEntries(sampleEntries());
    QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);

    model.setNlink(1, 3);

    QCOMPARE(spy.count(), 1);
    const auto args = spy.takeFirst();
    const QModelIndex topLeft = args.at(0).toModelIndex();
    QCOMPARE(topLeft.row(), 1);
    QCOMPARE(topLeft.column(), int(FileListModel::LinksColumn));
    const auto roles = args.at(2).value<QList<int>>();
    QVERIFY(roles.contains(Qt::DisplayRole));
    QVERIFY(roles.contains(int(FileListModel::SortRole)));
    QCOMPARE(model.data(model.index(1, FileListModel::LinksColumn),
                        Qt::DisplayRole).toString(), "3");
}

void TestFileListModel::setNlinkSameValueIsSilent()
{
    FileListModel model;
    model.setEntries(sampleEntries());
    QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);
    model.setNlink(2, 2); // row 2 already has nlink 2
    QCOMPARE(spy.count(), 0);
}

void TestFileListModel::setNlinkIgnoresBadRows()
{
    FileListModel model;
    model.setEntries(sampleEntries());
    QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);
    model.setNlink(-1, 2);
    model.setNlink(model.rowCount(), 2);
    QCOMPARE(spy.count(), 0);
}

void TestFileListModel::setIconModeUpdatesIconRole()
{
    FileListModel model;
    model.setEntries(sampleEntries());
    QCOMPARE(model.iconMode(), FileListModel::IconMode::Os); // default
    QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);

    model.setIconMode(FileListModel::IconMode::Generic);

    QCOMPARE(model.iconMode(), FileListModel::IconMode::Generic);
    QCOMPARE(spy.count(), 1);
    const auto args = spy.takeFirst();
    const QModelIndex topLeft = args.at(0).toModelIndex();
    const QModelIndex bottomRight = args.at(1).toModelIndex();
    QCOMPARE(topLeft.row(), 0);
    QCOMPARE(topLeft.column(), int(FileListModel::IconColumn));
    QCOMPARE(bottomRight.row(), model.rowCount() - 1);
    QCOMPARE(bottomRight.column(), int(FileListModel::IconColumn));
    const auto roles = args.at(2).value<QList<int>>();
    QVERIFY(roles.contains(Qt::DecorationRole));
}

void TestFileListModel::setIconModeSameValueIsSilent()
{
    FileListModel model;
    model.setEntries(sampleEntries());
    QSignalSpy spy(&model, &QAbstractItemModel::dataChanged);
    model.setIconMode(FileListModel::IconMode::Os); // already the default
    QCOMPARE(spy.count(), 0);
}

SMBMGR_TEST_MAIN(TestFileListModel)

#include "tst_filelistmodel.moc"
