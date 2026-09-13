#pragma once

#include <QSortFilterProxyModel>

// Sort/filter layer over FileListModel: by default folders group above files
// no matter which column or direction is sorted (see setFoldersFirst); within
// each group the usual column comparison applies (case-insensitive for names
// unless setSortCaseSensitivity changes it). Filtering is the inherited
// fixed-string "contains" match, case-insensitive, on the name.
class FileFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit FileFilterProxyModel(QObject *parent = nullptr);

    // On (the default): folders group above files. Off: folders sort among
    // the files like any other entry.
    void setFoldersFirst(bool foldersFirst);
    bool foldersFirst() const { return m_foldersFirst; }

    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    Qt::SortOrder m_order = Qt::AscendingOrder;
    bool m_foldersFirst = true;
};
