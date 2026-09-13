#pragma once

#include <QHash>
#include <QSet>
#include <QWidget>

#include "models/FileListModel.h" // for the nested IconMode enum (setIconMode/signal)
#include "smb/SmbTypes.h"

class QAction;
class QItemSelectionModel;
class QLabel;
class QLineEdit;
class QModelIndex;
class QStatusBar;
class QToolButton;
class QTreeView;

class FileFilterProxyModel;
class SmbSession;

// One filesystem view: path box, case-insensitive filter box, and the file
// table, with a status bar below showing Selected/Visible/Total counts. The
// full listing stays in memory; the proxy decides what is shown.
class FileBrowserView : public QWidget
{
    Q_OBJECT

public:
    explicit FileBrowserView(SmbSession *session, QWidget *parent = nullptr);

    void navigateTo(const QString &path);
    void refresh(); // re-lists the current path (e.g. after linking)

    // Navigates to folderPath (if not already there) and then selects and
    // scrolls to fileName once the listing is in.
    void navigateToAndReveal(const QString &folderPath, const QString &fileName);

    QString currentPath() const { return m_currentPath; }

    // Applies mode to this view's model and updates the View menu's checked
    // action. MainWindow owns broadcasting a mode change to every view (see
    // iconModeChangeRequested) so both views' icons and checkmarks agree.
    void setIconMode(FileListModel::IconMode mode);

signals:
    void errorOccurred(const QString &message); // surfaced in the main status bar
    void selectionChanged(); // fires on user selection and on listing changes

    // The user picked a View menu option; not applied locally (see setIconMode).
    void iconModeChangeRequested(FileListModel::IconMode mode);

private:
    void onPathEdited();
    void onEntryActivated(const QModelIndex &proxyIndex);
    void onDirectoryListed(const QString &path, const QList<FileEntry> &entries);
    void onDirectoryListFailed(const QString &path, const QString &message);
    void onFileStatted(const QString &path, int nlink, quint64 inode);
    void onStatFailed(const QString &path, const QString &message);
    void updateStatusBar();
    void updateSelectedCount();

    QString entryPath(const QString &name) const;
    void revealByName(const QString &fileName);
    void resetStatQueue();
    void pumpStats();
    int nextStatRow();

    SmbSession *m_session = nullptr;
    FileListModel *m_model = nullptr;
    FileFilterProxyModel *m_proxy = nullptr;
    QTreeView *m_tree = nullptr;
    QToolButton *m_upButton = nullptr;
    QLineEdit *m_pathEdit = nullptr;
    QToolButton *m_sortButton = nullptr;
    QToolButton *m_viewButton = nullptr;
    QAction *m_viewIconsOsAction = nullptr;
    QAction *m_viewIconsGenericAction = nullptr;
    QLineEdit *m_filterEdit = nullptr;
    QStatusBar *m_statusBar = nullptr;
    QLabel *m_selectedLabel = nullptr;
    QLabel *m_visibleLabel = nullptr;
    QLabel *m_totalLabel = nullptr;
    QString m_currentPath;   // last successfully listed path
    QString m_pendingPath;   // path of the in-flight listing, empty if none
    QString m_pendingReveal; // file to select once the pending listing lands

    // Lazy link-count fill-in: a bounded number of stats is in
    // flight, visible rows are served first, and navigation resets everything
    // (replies for the old directory miss m_statInFlight and are dropped).
    QList<int> m_statOrder;          // source rows of all files, listing order
    int m_statCursor = 0;            // next sequential candidate in m_statOrder
    QSet<int> m_statRequested;       // source rows already sent (or done)
    QHash<QString, int> m_statInFlight; // full path -> source row
};
