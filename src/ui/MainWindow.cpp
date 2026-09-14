#include "MainWindow.h"

#include "ui/AboutDialog.h"
#include "ui/FileBrowserView.h"
#include "ui/IconUtil.h"
#include "ui/MatchFinderPanel.h"

#include <QAction>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QScreen>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QUrl>

#include <utility>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("SMB Manager"));

    m_session = new SmbSession(this);
    connect(m_session, &SmbSession::stateChanged,
            this, &MainWindow::onSessionStateChanged);
    connect(m_session, &SmbSession::errorOccurred,
            this, &MainWindow::onSessionError);

    m_toolBar = addToolBar(tr("Main"));
    m_toolBar->setMovable(false);
    m_toolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    m_connectAction = m_toolBar->addAction(QString());
    m_connectAction->setObjectName(QStringLiteral("mw.connectAction"));
    connect(m_connectAction, &QAction::triggered,
        this, &MainWindow::onConnectActionTriggered);

    m_urlEdit = new QLineEdit(this);
    m_urlEdit->setObjectName(QStringLiteral("mw.urlEdit"));
    m_urlEdit->setPlaceholderText(QStringLiteral("smb://user@host:port/share"));
    m_urlEdit->setClearButtonEnabled(true);
    m_urlEdit->setText(QSettings().value(QStringLiteral("connect/lastUrl")).toString());
    m_urlEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolBar->addWidget(m_urlEdit);

    connect(m_urlEdit, &QLineEdit::returnPressed, this, [this] {
        if (m_session->state() == SmbSession::State::Disconnected) {
            m_connectAction->trigger();
        }
    });

    m_toolBar->addSeparator();

    m_aboutAction = m_toolBar->addAction(tr("About"));
    m_aboutAction->setObjectName(QStringLiteral("mw.aboutAction"));
    connect(m_aboutAction, &QAction::triggered,
            this, &MainWindow::onAboutActionTriggered);

    m_spinnerTimer = new QTimer(this);
    m_spinnerTimer->setInterval(80);
    connect(m_spinnerTimer, &QTimer::timeout, this, &MainWindow::advanceSpinner);

    // Default password prompt: the modal dialog. Tests swap this out via
    // setPasswordPrompt().
    m_passwordPrompt = [this](const QString &shareDisplayName) -> std::optional<QString> {
        bool ok = false;
        const QString password = QInputDialog::getText(
            this, tr("Connect to SMB Share"),
            tr("Password for %1\n(leave empty if none is needed):").arg(shareDisplayName),
            QLineEdit::Password, QString(), &ok);
        if (!ok) {
            return std::nullopt;
        }
        return password;
    };

    statusBar(); // create it up front so messages have somewhere to go
    onSessionStateChanged(SmbSession::State::Disconnected); // creates the placeholder
    restoreWindowGeometry();
}

void MainWindow::setPasswordPrompt(PasswordPrompt prompt)
{
    if (prompt) {
        m_passwordPrompt = std::move(prompt);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings().setValue(QStringLiteral("window/geometry"), saveGeometry());
    saveSplitterState();
    QMainWindow::closeEvent(event);
}

void MainWindow::saveSplitterState()
{
    if (m_hSplitter) {
        QSettings().setValue(QStringLiteral("window/hsplitterState"),
                             m_hSplitter->saveState());
    }
}

// Restores the last saved size/position, but only if enough of the title bar
// would land on a currently-connected screen for the user to grab and drag it
// back — otherwise a monitor that's since been unplugged (undocked laptop,
// changed RDP resolution, ...) would strand the window off-screen with no way
// to reach it. Falls back to a centered default in that case.
void MainWindow::restoreWindowGeometry()
{
    const QByteArray saved = QSettings().value(QStringLiteral("window/geometry")).toByteArray();
    const QSize defaultSize(900, 600);

    if (saved.isEmpty() || !restoreGeometry(saved)) {
        resize(defaultSize);
        return;
    }

    const int kMinVisiblePx = 60;
    QRect titleStrip = frameGeometry();
    titleStrip.setHeight(qMin(titleStrip.height(), kMinVisiblePx));

    bool reachable = false;
    for (const QScreen *screen : QGuiApplication::screens()) {
        const QRect overlap = titleStrip.intersected(screen->availableGeometry());
        if (overlap.width() >= kMinVisiblePx && overlap.height() >= kMinVisiblePx) {
            reachable = true;
            break;
        }
    }

    if (!reachable) {
        resize(defaultSize);
        const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
        move(avail.center() - QPoint(defaultSize.width() / 2, defaultSize.height() / 2));
    }
}

void MainWindow::onConnectActionTriggered()
{
    switch (m_session->state()) {
    case SmbSession::State::Disconnected: {
        const QString text = m_urlEdit->text().trimmed();
        if (text.isEmpty()) {
            statusBar()->showMessage(tr("Enter a share URL first, e.g. smb://user@host/share"));
            m_urlEdit->setFocus();
            return;
        }
        QString error;
        const auto spec = SmbShareSpec::fromUrl(QUrl(text), &error);
        if (!spec) {
            statusBar()->showMessage(error);
            m_urlEdit->setFocus();
            return;
        }
        const std::optional<QString> password = m_passwordPrompt(spec->displayName());
        if (!password) {
            return; // prompt cancelled
        }
        m_shareDisplayName = spec->displayName();
        m_session->connectToShare(*spec, *password);
        break;
    }
    case SmbSession::State::Connecting:
        m_session->abortConnect();
        break;
    case SmbSession::State::Connected:
        m_session->disconnectFromShare();
        break;
    }
}

void MainWindow::onSessionStateChanged(SmbSession::State state)
{
    switch (state) {
    case SmbSession::State::Disconnected:
        m_spinnerTimer->stop();
        m_connectAction->setText(tr("Connect"));
        m_connectAction->setToolTip(tr("Connect to the SMB share"));
        m_connectAction->setIcon(coloredIcon(QStringLiteral(":/icons/desktop_windows.svg"),
                                              palette().color(QPalette::ButtonText),
                                              m_toolBar->iconSize(), devicePixelRatioF()));
        m_urlEdit->setEnabled(true);
        if (!m_centralLabel) {
            saveSplitterState();
            m_centralLabel = new QLabel(this);
            m_centralLabel->setAlignment(Qt::AlignCenter);
            m_centralLabel->setEnabled(false); // renders the placeholder text greyed out
            setCentralWidget(m_centralLabel); // deletes the splitters, views, and panel
            m_hSplitter = nullptr;
            m_splitter = nullptr;
            m_matchPanel = nullptr;
            m_views.clear();
        }
        m_centralLabel->setText(tr("Not connected.\nEnter smb://user@host:port/share and press Connect."));
        statusBar()->showMessage(tr("Disconnected."));
        break;
    case SmbSession::State::Connecting:
        m_spinnerAngle = 0;
        advanceSpinner();
        m_spinnerTimer->start();
        m_connectAction->setText(tr("Abort"));
        m_connectAction->setToolTip(tr("Abort the connection attempt"));
        m_urlEdit->setEnabled(false);
        m_centralLabel->setText(tr("Connecting to %1…").arg(m_shareDisplayName));
        statusBar()->showMessage(tr("Connecting to %1…").arg(m_shareDisplayName));
        break;
    case SmbSession::State::Connected:
        m_spinnerTimer->stop();
        m_connectAction->setText(tr("Disconnect"));
        m_connectAction->setToolTip(tr("Disconnect from the share"));
        m_connectAction->setIcon(coloredIcon(QStringLiteral(":/icons/desktop_access_disabled.svg"),
                                              palette().color(QPalette::ButtonText),
                                              m_toolBar->iconSize(), devicePixelRatioF()));
        m_urlEdit->setEnabled(false);
        QSettings().setValue(QStringLiteral("connect/lastUrl"), m_urlEdit->text().trimmed());
        m_hSplitter = new QSplitter(Qt::Horizontal, this);
        m_hSplitter->setChildrenCollapsible(false);
        m_splitter = new QSplitter(Qt::Vertical, m_hSplitter);
        m_splitter->setChildrenCollapsible(false);
        m_matchPanel = new MatchFinderPanel(m_session, m_hSplitter);
        m_hSplitter->addWidget(m_matchPanel);
        m_hSplitter->addWidget(m_splitter);
        // Half the width each by default. setSizes() values act as relative
        // weights; they only need to exceed both minimum widths, which would
        // otherwise take precedence.
        m_hSplitter->setSizes({10000, 10000});
        connect(m_matchPanel, &MatchFinderPanel::revealRequested,
                this, &MainWindow::onRevealRequested);
        connect(m_matchPanel, &MatchFinderPanel::linkRunFinished, this, [this] {
            // Any view may be showing affected files or link counts.
            for (FileBrowserView *view : std::as_const(m_views)) {
                view->refresh();
            }
        });
        connect(m_matchPanel, &MatchFinderPanel::statusMessage, this,
                [this](const QString &message) { statusBar()->showMessage(message); });
        setCentralWidget(m_hSplitter); // deletes the placeholder label
        m_centralLabel = nullptr;
        {
            const QByteArray state =
                QSettings().value(QStringLiteral("window/hsplitterState")).toByteArray();
            if (!state.isEmpty()) {
                m_hSplitter->restoreState(state);
            }
        }

        // The fixed pair of views (ADR 0003).
        addView();
        addView();

        m_matchPanel->beginPathValidation();
        statusBar()->showMessage(tr("Connected to %1.").arg(m_shareDisplayName));
        break;
    }
}

void MainWindow::addView()
{
    if (!m_splitter) {
        return;
    }
    auto *view = new FileBrowserView(m_session, this);
    connect(view, &FileBrowserView::errorOccurred,
            this, &MainWindow::onSessionError);
    connect(view, &FileBrowserView::iconModeChangeRequested,
            this, &MainWindow::onIconModeChangeRequested);
    view->setIconMode(currentIconMode());
    m_views.append(view);
    m_splitter->addWidget(view);
    view->navigateTo(QStringLiteral("/"));
}

FileListModel::IconMode MainWindow::currentIconMode() const
{
    return static_cast<FileListModel::IconMode>(
        QSettings().value(QStringLiteral("view/iconMode"), int(FileListModel::IconMode::Os)).toInt());
}

void MainWindow::onIconModeChangeRequested(FileListModel::IconMode mode)
{
    QSettings().setValue(QStringLiteral("view/iconMode"), int(mode));
    for (FileBrowserView *view : std::as_const(m_views)) {
        view->setIconMode(mode);
    }
}

void MainWindow::onAboutActionTriggered()
{
    AboutDialog dialog(this);
    dialog.exec();
}

// A Match Finder result row was selected: the first view shows the primary
// file, the second shows the secondary (added if only one view is open).
void MainWindow::onRevealRequested(const QString &primaryFolder, const QString &primaryName,
                                   const QString &secondaryFolder, const QString &secondaryName)
{
    if (m_views.size() < 2) {
        addView();
    }
    if (m_views.size() < 2) {
        return; // not connected
    }
    m_views[0]->navigateToAndReveal(primaryFolder, primaryName);
    m_views[1]->navigateToAndReveal(secondaryFolder, secondaryName);
}

// The session emits stateChanged(Disconnected) before errorOccurred(), so the
// error lands after (and overwrites) the plain "Disconnected." message.
void MainWindow::onSessionError(const QString &message)
{
    statusBar()->showMessage(message);
}

void MainWindow::advanceSpinner()
{
    m_spinnerAngle = (m_spinnerAngle + 30) % 360;
    m_connectAction->setIcon(QIcon(spinnerPixmap(m_spinnerAngle)));
}

// A simple painted spinner (rotating open arc) so the connecting state needs
// no icon assets.
QPixmap MainWindow::spinnerPixmap(int angleDegrees) const
{
    const QSize logical = m_toolBar->iconSize();
    const qreal dpr = devicePixelRatioF();
    QPixmap pixmap(logical * dpr);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    const qreal penWidth = qMax<qreal>(2.0, logical.width() / 8.0);
    QRectF arcRect(QPointF(0, 0), QSizeF(logical));
    const qreal margin = penWidth / 2.0 + 1.0;
    arcRect.adjust(margin, margin, -margin, -margin);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(palette().color(QPalette::Highlight), penWidth,
                        Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(arcRect, -angleDegrees * 16, 300 * 16);
    return pixmap;
}
