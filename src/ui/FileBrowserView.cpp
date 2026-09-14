#include "FileBrowserView.h"

#include <QAction>
#include <QActionGroup>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFontMetrics>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QScrollBar>
#include <QStatusBar>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace {

// Upper bound on stats in flight per view. High enough to keep the pipe full
// on a LAN, low enough that navigation away leaves little wasted work (the
// unsent backlog is simply dropped).
constexpr int kMaxStatsInFlight = 32;

} // namespace

#include "core/PathUtil.h"
#include "models/FileFilterProxyModel.h"
#include "models/FileListModel.h"
#include "smb/SmbSession.h"
#include "ui/IconUtil.h"

FileBrowserView::FileBrowserView(SmbSession *session, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
{
    m_model = new FileListModel(this);
    m_proxy = new FileFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);

    m_upButton = new QToolButton(this);
    m_upButton->setObjectName(QStringLiteral("fbv.upButton"));
    m_upButton->setIcon(coloredIcon(QStringLiteral(":/icons/folder_parent.svg"),
                                    palette().color(QPalette::ButtonText),
                                    m_upButton->iconSize(), devicePixelRatioF()));
    m_upButton->setToolTip(tr("Go up to the parent folder"));
    m_upButton->setEnabled(false); // no parent until a listing below "/" succeeds
    connect(m_upButton, &QToolButton::clicked, this, [this] {
        if (m_currentPath.isEmpty() || m_currentPath == QLatin1String("/")) {
            return;
        }
        const int slash = m_currentPath.lastIndexOf(QLatin1Char('/'));
        navigateTo(slash <= 0 ? QStringLiteral("/") : m_currentPath.left(slash));
    });

    m_pathEdit = new QLineEdit(QStringLiteral("/"), this);
    m_pathEdit->setObjectName(QStringLiteral("fbv.pathEdit"));
    connect(m_pathEdit, &QLineEdit::returnPressed,
            this, &FileBrowserView::onPathEdited);

    m_sortButton = new QToolButton(this);
    m_sortButton->setIcon(coloredIcon(QStringLiteral(":/icons/list_arrow.svg"),
                                      palette().color(QPalette::ButtonText),
                                      m_sortButton->iconSize(), devicePixelRatioF()));
    m_sortButton->setText(tr("Sort"));
    m_sortButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_sortButton->setToolTip(tr("Sort options"));
    m_sortButton->setAutoRaise(true);
    m_sortButton->setPopupMode(QToolButton::InstantPopup);

    auto *sortMenu = new QMenu(m_sortButton);

    sortMenu->addSection(tr("Folders"));
    auto *foldersGroup = new QActionGroup(sortMenu);
    QAction *foldersOnTop = sortMenu->addAction(tr("Folders on Top"));
    foldersOnTop->setObjectName(QStringLiteral("fbv.sortFoldersOnTop"));
    foldersOnTop->setCheckable(true);
    foldersOnTop->setChecked(true); // matches FileFilterProxyModel's default
    foldersGroup->addAction(foldersOnTop);
    QAction *foldersAmongFiles = sortMenu->addAction(tr("Folders Sorted with Files"));
    foldersAmongFiles->setObjectName(QStringLiteral("fbv.sortFoldersAmongFiles"));
    foldersAmongFiles->setCheckable(true);
    foldersGroup->addAction(foldersAmongFiles);
    connect(foldersOnTop, &QAction::triggered, this, [this] { m_proxy->setFoldersFirst(true); });
    connect(foldersAmongFiles, &QAction::triggered, this, [this] { m_proxy->setFoldersFirst(false); });

    sortMenu->addSection(tr("Case"));
    auto *caseGroup = new QActionGroup(sortMenu);
    QAction *caseInsensitive = sortMenu->addAction(tr("Case-Insensitive"));
    caseInsensitive->setObjectName(QStringLiteral("fbv.sortCaseInsensitive"));
    caseInsensitive->setCheckable(true);
    caseInsensitive->setChecked(true); // matches FileFilterProxyModel's default
    caseGroup->addAction(caseInsensitive);
    QAction *caseSensitive = sortMenu->addAction(tr("Case-Sensitive"));
    caseSensitive->setObjectName(QStringLiteral("fbv.sortCaseSensitive"));
    caseSensitive->setCheckable(true);
    caseGroup->addAction(caseSensitive);
    connect(caseInsensitive, &QAction::triggered, this,
            [this] { m_proxy->setSortCaseSensitivity(Qt::CaseInsensitive); });
    connect(caseSensitive, &QAction::triggered, this,
            [this] { m_proxy->setSortCaseSensitivity(Qt::CaseSensitive); });

    m_sortButton->setMenu(sortMenu);

    m_viewButton = new QToolButton(this);
    m_viewButton->setIcon(coloredIcon(QStringLiteral(":/icons/content_view.svg"),
                                      palette().color(QPalette::ButtonText),
                                      m_viewButton->iconSize(), devicePixelRatioF()));
    m_viewButton->setText(tr("View"));
    m_viewButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_viewButton->setToolTip(tr("Icon options"));
    m_viewButton->setAutoRaise(true);
    m_viewButton->setPopupMode(QToolButton::InstantPopup);

    auto *viewMenu = new QMenu(m_viewButton);
    auto *iconModeGroup = new QActionGroup(viewMenu);
    m_viewIconsOsAction = viewMenu->addAction(tr("Use OS file-type icons"));
    m_viewIconsOsAction->setObjectName(QStringLiteral("fbv.viewIconsOs"));
    m_viewIconsOsAction->setCheckable(true);
    m_viewIconsOsAction->setChecked(true); // matches FileListModel's default
    iconModeGroup->addAction(m_viewIconsOsAction);
    m_viewIconsGenericAction = viewMenu->addAction(tr("Use generic icon"));
    m_viewIconsGenericAction->setObjectName(QStringLiteral("fbv.viewIconsGeneric"));
    m_viewIconsGenericAction->setCheckable(true);
    iconModeGroup->addAction(m_viewIconsGenericAction);
    connect(m_viewIconsOsAction, &QAction::triggered, this,
            [this] { emit iconModeChangeRequested(FileListModel::IconMode::Os); });
    connect(m_viewIconsGenericAction, &QAction::triggered, this,
            [this] { emit iconModeChangeRequested(FileListModel::IconMode::Generic); });

    m_viewButton->setMenu(viewMenu);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setObjectName(QStringLiteral("fbv.filterEdit"));
    m_filterEdit->setPlaceholderText(tr("Filter"));
    m_filterEdit->setClearButtonEnabled(true);
    connect(m_filterEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_proxy->setFilterFixedString(text);
        updateStatusBar();
        updateSelectedCount(); // filtering can drop selected rows out of view
    });

    m_tree = new QTreeView(this);
    m_tree->setObjectName(QStringLiteral("fbv.tree"));
    m_tree->setModel(m_proxy);
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setAllColumnsShowFocus(true);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(FileListModel::NameColumn, Qt::AscendingOrder);
    connect(m_tree, &QTreeView::activated,
            this, &FileBrowserView::onEntryActivated);
    connect(m_tree->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        updateSelectedCount();
        emit selectionChanged();
    });

    QHeaderView *header = m_tree->header();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(FileListModel::NameColumn, QHeaderView::Stretch);
    header->setSectionResizeMode(FileListModel::IconColumn, QHeaderView::Fixed);
    header->resizeSection(FileListModel::IconColumn,
                          m_tree->iconSize().isValid() ? m_tree->iconSize().width() + 12 : 28);

    auto *toolbarLayout = new QHBoxLayout;
    toolbarLayout->addWidget(m_upButton);
    toolbarLayout->addWidget(m_pathEdit, /*stretch*/ 3);
    toolbarLayout->addWidget(m_sortButton);
    toolbarLayout->addWidget(m_viewButton);
    toolbarLayout->addWidget(m_filterEdit, /*stretch*/ 1);

    m_statusBar = new QStatusBar(this);
    m_statusBar->setObjectName(QStringLiteral("fbv.statusBar"));
    m_statusBar->setSizeGripEnabled(false);
    m_selectedLabel = new QLabel(m_statusBar);
    m_selectedLabel->setObjectName(QStringLiteral("fbv.selectedLabel"));
    m_visibleLabel = new QLabel(m_statusBar);
    m_visibleLabel->setObjectName(QStringLiteral("fbv.visibleLabel"));
    m_totalLabel = new QLabel(m_statusBar);
    m_totalLabel->setObjectName(QStringLiteral("fbv.totalLabel"));

    // Floor each label's width at its 5-digit worst case so ordinary count
    // changes don't shift the others; still free to grow past that if a
    // count ever needs more digits.
    const QFontMetrics statusMetrics(m_selectedLabel->font());
    m_selectedLabel->setMinimumWidth(statusMetrics.horizontalAdvance(tr("Selected: %1").arg(99999)));
    m_visibleLabel->setMinimumWidth(statusMetrics.horizontalAdvance(tr("Visible: %1").arg(99999)));
    m_totalLabel->setMinimumWidth(statusMetrics.horizontalAdvance(tr("Total: %1").arg(99999)));

    m_statusBar->addPermanentWidget(m_selectedLabel);
    m_statusBar->addPermanentWidget(m_visibleLabel);
    m_statusBar->addPermanentWidget(m_totalLabel);

    auto *groupBox = new QGroupBox(tr("File Browser View"), this);
    auto *groupLayout = new QVBoxLayout(groupBox);
    groupLayout->setContentsMargins(4, 0, 4, 0);
    groupLayout->addLayout(toolbarLayout);
    groupLayout->addWidget(m_tree);
    groupLayout->setSpacing(0);
    groupLayout->addWidget(m_statusBar);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(groupBox);

    connect(m_session, &SmbSession::directoryListed,
            this, &FileBrowserView::onDirectoryListed);
    connect(m_session, &SmbSession::directoryListFailed,
            this, &FileBrowserView::onDirectoryListFailed);
    connect(m_session, &SmbSession::fileStatted,
            this, &FileBrowserView::onFileStatted);
    connect(m_session, &SmbSession::statFailed,
            this, &FileBrowserView::onStatFailed);

    // Scrolling changes which rows are visible; freed-up slots should go to
    // them first.
    connect(m_tree->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this] { pumpStats(); });

    updateStatusBar();
    updateSelectedCount();
}

void FileBrowserView::refresh()
{
    if (!m_currentPath.isEmpty()) {
        navigateTo(m_currentPath);
    }
}

void FileBrowserView::setIconMode(FileListModel::IconMode mode)
{
    m_model->setIconMode(mode);

    // setChecked() doesn't emit triggered(), so this can't re-enter
    // MainWindow's broadcast. Don't block signals: QActionGroup relies on them
    // to uncheck the sibling action.
    QAction *checkedAction = mode == FileListModel::IconMode::Os
        ? m_viewIconsOsAction
        : m_viewIconsGenericAction;
    checkedAction->setChecked(true);
}

void FileBrowserView::navigateTo(const QString &path)
{
    const QString normalized = pathutil::normalize(path);
    m_pendingReveal.clear(); // only navigateToAndReveal carries a selection over
    m_pendingPath = normalized;
    m_pathEdit->setText(normalized);
    m_session->listDirectory(normalized);
}

void FileBrowserView::navigateToAndReveal(const QString &folderPath, const QString &fileName)
{
    const QString normalized = pathutil::normalize(folderPath);
    if (normalized == m_currentPath && m_pendingPath.isEmpty()) {
        revealByName(fileName);
        return;
    }
    navigateTo(normalized);
    m_pendingReveal = fileName;
}

void FileBrowserView::onPathEdited()
{
    navigateTo(m_pathEdit->text());
}

void FileBrowserView::onEntryActivated(const QModelIndex &proxyIndex)
{
    if (!proxyIndex.isValid()) {
        return;
    }
    const FileEntry &entry = m_model->entryAt(m_proxy->mapToSource(proxyIndex).row());
    if (entry.isDir) {
        navigateTo(entryPath(entry.name));
    }
}

void FileBrowserView::onDirectoryListed(const QString &path, const QList<FileEntry> &entries)
{
    if (path != m_pendingPath) {
        return; // a reply for a path this view no longer wants
    }
    m_pendingPath.clear();
    m_currentPath = path;
    m_pathEdit->setText(path);
    m_upButton->setEnabled(path != QLatin1String("/"));
    m_model->setEntries(entries);
    updateStatusBar();
    resetStatQueue();
    if (!m_pendingReveal.isEmpty()) {
        revealByName(m_pendingReveal);
        m_pendingReveal.clear();
    }
    // The model reset cleared the selection without a selectionChanged signal
    // (QItemSelectionModel::reset is documented not to emit); tell listeners.
    updateSelectedCount();
    emit selectionChanged();
}

void FileBrowserView::onDirectoryListFailed(const QString &path, const QString &message)
{
    if (path != m_pendingPath) {
        return;
    }
    m_pendingPath.clear();
    m_pendingReveal.clear();
    // Keep showing the last good listing; put its path back in the box.
    m_pathEdit->setText(m_currentPath.isEmpty() ? QStringLiteral("/") : m_currentPath);
    emit errorOccurred(message);
}

void FileBrowserView::onFileStatted(const QString &path, int nlink, quint64 inode)
{
    Q_UNUSED(inode);
    const auto it = m_statInFlight.constFind(path);
    if (it == m_statInFlight.constEnd()) {
        return; // stale: a reply for a directory we've navigated away from
    }
    const int row = it.value();
    m_statInFlight.erase(it);
    m_model->setNlink(row, nlink);
    pumpStats();
}

// A failed stat (e.g. no permission) shows as "?" — no status-bar noise, a
// whole directory of them would flood it.
void FileBrowserView::onStatFailed(const QString &path, const QString &message)
{
    Q_UNUSED(message);
    const auto it = m_statInFlight.constFind(path);
    if (it == m_statInFlight.constEnd()) {
        return;
    }
    const int row = it.value();
    m_statInFlight.erase(it);
    m_model->setNlink(row, FileEntry::kNlinkUnavailable);
    pumpStats();
}

QString FileBrowserView::entryPath(const QString &name) const
{
    return pathutil::join(m_currentPath, name);
}

// Selects and scrolls to the entry with the given name, clearing the filter
// if it currently hides that entry. Silently does nothing if no entry matches
// (e.g. the file was renamed since the search ran).
void FileBrowserView::revealByName(const QString &fileName)
{
    for (int row = 0; row < m_model->rowCount(); ++row) {
        if (m_model->entryAt(row).name != fileName) {
            continue;
        }
        QModelIndex proxyIndex = m_proxy->mapFromSource(m_model->index(row, 0));
        if (!proxyIndex.isValid()) {
            m_filterEdit->clear();
            proxyIndex = m_proxy->mapFromSource(m_model->index(row, 0));
        }
        if (proxyIndex.isValid()) {
            m_tree->selectionModel()->setCurrentIndex(
                proxyIndex,
                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            m_tree->scrollTo(proxyIndex, QAbstractItemView::PositionAtCenter);
        }
        return;
    }
}

void FileBrowserView::resetStatQueue()
{
    // In-flight replies for the old directory now miss m_statInFlight and are
    // dropped; the window may transiently hold old + new requests, bounded by
    // 2 * kMaxStatsInFlight.
    m_statInFlight.clear();
    m_statRequested.clear();
    m_statOrder.clear();
    m_statCursor = 0;
    for (int row = 0; row < m_model->rowCount(); ++row) {
        if (!m_model->entryAt(row).isDir) {
            m_statOrder.append(row);
        }
    }
    pumpStats();
}

void FileBrowserView::pumpStats()
{
    while (m_statInFlight.size() < kMaxStatsInFlight) {
        const int row = nextStatRow();
        if (row < 0) {
            return;
        }
        const QString path = entryPath(m_model->entryAt(row).name);
        m_statRequested.insert(row);
        m_statInFlight.insert(path, row);
        m_session->statFile(path);
    }
}

// Picks the next row needing a stat: visible rows first, then listing order.
int FileBrowserView::nextStatRow()
{
    const int viewportBottom = m_tree->viewport()->height();
    QModelIndex idx = m_tree->indexAt(QPoint(0, 0));
    while (idx.isValid() && m_tree->visualRect(idx).top() < viewportBottom) {
        const int row = m_proxy->mapToSource(idx).row();
        const FileEntry &entry = m_model->entryAt(row);
        if (!entry.isDir && entry.nlink == FileEntry::kNlinkUnknown
            && !m_statRequested.contains(row)) {
            return row;
        }
        idx = m_tree->indexBelow(idx);
    }

    while (m_statCursor < m_statOrder.size()) {
        const int row = m_statOrder.at(m_statCursor);
        if (!m_statRequested.contains(row)) {
            return row;
        }
        ++m_statCursor;
    }
    return -1;
}

void FileBrowserView::updateStatusBar()
{
    m_visibleLabel->setText(tr("Visible: %1").arg(m_proxy->rowCount()));
    m_totalLabel->setText(tr("Total: %1").arg(m_model->rowCount()));
}

void FileBrowserView::updateSelectedCount()
{
    m_selectedLabel->setText(tr("Selected: %1").arg(m_tree->selectionModel()->selectedRows().size()));
}
