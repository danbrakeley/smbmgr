#pragma once

#include <QAbstractTableModel>
#include <QIcon>
#include <QList>

#include "smb/SmbTypes.h"

// Holds the full in-memory file list of one filesystem view (the view keeps
// everything, the filter proxy decides what is shown).
class FileListModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column {
        IconColumn = 0,
        NameColumn,
        SizeColumn,
        ModifiedColumn,
        LinksColumn,
        InodeColumn,
        ColumnCount,
    };

    // Roles the sort/filter proxy works with, so display formatting (human
    // sizes, locale dates, the "…" placeholder) never affects ordering.
    enum Role {
        SortRole = Qt::UserRole,
        IsDirRole,
    };

    // How the icon column's Qt::DecorationRole is computed. Os asks the host
    // OS for a per-extension icon (see IconUtil::osIcon); Generic uses QStyle's
    // standard folder/file icons.
    enum class IconMode {
        Os,
        Generic,
    };

    explicit FileListModel(QObject *parent = nullptr);

    void setEntries(const QList<FileEntry> &entries);
    const FileEntry &entryAt(int row) const { return m_entries.at(row); }

    // Lazy stat results land here, one row at a time.
    void setNlink(int row, int nlink);

    void setIconMode(IconMode mode);
    IconMode iconMode() const { return m_iconMode; }

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    QList<FileEntry> m_entries;
    QIcon m_dirIcon;
    QIcon m_fileIcon;
    IconMode m_iconMode = IconMode::Os;
};
