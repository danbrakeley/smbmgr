#include <QtTest>

#include <QDateTime>
#include <QStringList>

#include "models/FileFilterProxyModel.h"
#include "models/FileListModel.h"

#include "common/TestMain.h"

namespace {

QList<FileEntry> sampleEntries()
{
    const QDateTime when = QDateTime::currentDateTime();
    return {
        // Mixed-case names on purpose: the case toggles are part of the
        // contract. Sizes are chosen so value-sort differs from text-sort.
        {"beta_dir", true, 0, when, 1, FileEntry::kNlinkUnknown},
        {"Alpha_dir", true, 0, when, 2, FileEntry::kNlinkUnknown},
        {"aaa.txt", false, 9, when, 3, FileEntry::kNlinkUnknown},
        {"Bbb.txt", false, 100, when, 4, FileEntry::kNlinkUnknown},
        {"zzz.txt", false, 50, when, 5, FileEntry::kNlinkUnknown},
    };
}

QStringList namesInOrder(const FileFilterProxyModel &proxy)
{
    QStringList names;
    for (int row = 0; row < proxy.rowCount(); ++row) {
        names << proxy.index(row, FileListModel::NameColumn).data().toString();
    }
    return names;
}

} // namespace

class TestFileFilterProxyModel : public QObject
{
    Q_OBJECT

private slots:
    void foldersFirstAscending();
    void foldersFirstDescending();
    void foldersSortAmongFilesWhenOff();
    void toggleRegroupsWithStoredOrder();
    void caseSensitiveSort();
    void sizeSortsByValueNotText();
    void filterIsCaseInsensitiveContains();
};

void TestFileFilterProxyModel::foldersFirstAscending()
{
    FileListModel source;
    source.setEntries(sampleEntries());
    FileFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    proxy.sort(FileListModel::NameColumn, Qt::AscendingOrder);
    QCOMPARE(namesInOrder(proxy),
             QStringList({"Alpha_dir", "beta_dir", "aaa.txt", "Bbb.txt", "zzz.txt"}));
}

void TestFileFilterProxyModel::foldersFirstDescending()
{
    FileListModel source;
    source.setEntries(sampleEntries());
    FileFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    // Folders stay on top even in descending order (reversed among themselves).
    proxy.sort(FileListModel::NameColumn, Qt::DescendingOrder);
    QCOMPARE(namesInOrder(proxy),
             QStringList({"beta_dir", "Alpha_dir", "zzz.txt", "Bbb.txt", "aaa.txt"}));
}

void TestFileFilterProxyModel::foldersSortAmongFilesWhenOff()
{
    FileListModel source;
    source.setEntries(sampleEntries());
    FileFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    proxy.sort(FileListModel::NameColumn, Qt::AscendingOrder);
    proxy.setFoldersFirst(false);
    QCOMPARE(namesInOrder(proxy),
             QStringList({"aaa.txt", "Alpha_dir", "Bbb.txt", "beta_dir", "zzz.txt"}));
}

void TestFileFilterProxyModel::toggleRegroupsWithStoredOrder()
{
    FileListModel source;
    source.setEntries(sampleEntries());
    FileFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    // The regroup must respect the direction chosen before the toggle.
    proxy.sort(FileListModel::NameColumn, Qt::DescendingOrder);
    proxy.setFoldersFirst(false);
    QCOMPARE(namesInOrder(proxy),
             QStringList({"zzz.txt", "beta_dir", "Bbb.txt", "Alpha_dir", "aaa.txt"}));
    proxy.setFoldersFirst(true);
    QCOMPARE(namesInOrder(proxy),
             QStringList({"beta_dir", "Alpha_dir", "zzz.txt", "Bbb.txt", "aaa.txt"}));
}

void TestFileFilterProxyModel::caseSensitiveSort()
{
    FileListModel source;
    source.setEntries(sampleEntries());
    FileFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    // Uppercase sorts before lowercase under the Case-Sensitive sort option.
    proxy.setSortCaseSensitivity(Qt::CaseSensitive);
    proxy.sort(FileListModel::NameColumn, Qt::AscendingOrder);
    QCOMPARE(namesInOrder(proxy),
             QStringList({"Alpha_dir", "beta_dir", "Bbb.txt", "aaa.txt", "zzz.txt"}));
}

void TestFileFilterProxyModel::sizeSortsByValueNotText()
{
    FileListModel source;
    source.setEntries(sampleEntries());
    FileFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    // By value: 9 < 50 < 100. As text, "100" would sort before "50" and "9".
    proxy.sort(FileListModel::SizeColumn, Qt::AscendingOrder);
    const QStringList names = namesInOrder(proxy);
    QCOMPARE(names.mid(2), QStringList({"aaa.txt", "zzz.txt", "Bbb.txt"}));
}

void TestFileFilterProxyModel::filterIsCaseInsensitiveContains()
{
    FileListModel source;
    source.setEntries(sampleEntries());
    FileFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    proxy.setFilterFixedString("bb");
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, FileListModel::NameColumn).data().toString(), "Bbb.txt");
    // The status bar's Visible and Total labels read these two counts.
    QCOMPARE(source.rowCount(), 5);

    proxy.setFilterFixedString(QString());
    QCOMPARE(proxy.rowCount(), 5);
}

SMBMGR_TEST_MAIN(TestFileFilterProxyModel)

#include "tst_filefilterproxymodel.moc"
