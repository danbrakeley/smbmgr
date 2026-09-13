#include "MatchSearcher.h"

#include "core/PathUtil.h"
#include "smb/SmbSession.h"

namespace {

// Upper bound on listings in flight. Enumerations are heavier than stats, so
// this sits well below FileBrowserView's stat bound; enough to keep the pipe
// full on a LAN.
constexpr int kMaxListsInFlight = 8;

bool isUnder(const QString &path, const QString &root)
{
    if (root == QLatin1String("/")) {
        return true; // every share-absolute path is under the root
    }
    return path.startsWith(root) && path.size() > root.size()
           && path.at(root.size()) == QLatin1Char('/');
}

} // namespace

MatchSearcher::MatchSearcher(SmbSession *session, QObject *parent)
    : QObject(parent)
    , m_session(session)
{
    connect(m_session, &SmbSession::directoryListed,
            this, &MatchSearcher::onDirectoryListed);
    connect(m_session, &SmbSession::directoryListFailed,
            this, &MatchSearcher::onDirectoryListFailed);
}

void MatchSearcher::start(const Options &options)
{
    if (m_running) {
        return;
    }
    m_options = options;
    resetState();
    m_running = true;
    enqueueDir(m_options.primaryPath);
    enqueueDir(m_options.secondaryPath); // no-op when identical to primary
    pumpListings();
}

void MatchSearcher::cancel()
{
    if (!m_running) {
        return;
    }
    const int folderErrors = m_folderErrors;
    m_running = false;
    resetState(); // late replies now miss m_inFlight and are dropped
    emit finished({}, folderErrors, false, /*cancelled*/ true);
}

// A directory belongs to a side if it is that side's root, or below it when
// that side includes subfolders. Files directly inside the directory inherit
// its mask. Deriving the mask from the path (rather than propagating it down
// the traversal) keeps it correct when one root is nested inside the other:
// the nested root's tree is listed once with both bits set.
quint8 MatchSearcher::sideMaskOf(const QString &dirPath) const
{
    quint8 mask = 0;
    if (dirPath == m_options.primaryPath
        || (m_options.primaryRecursive && isUnder(dirPath, m_options.primaryPath))) {
        mask |= matchpairing::kPrimary;
    }
    if (dirPath == m_options.secondaryPath
        || (m_options.secondaryRecursive && isUnder(dirPath, m_options.secondaryPath))) {
        mask |= matchpairing::kSecondary;
    }
    return mask;
}

void MatchSearcher::enqueueDir(const QString &path)
{
    if (!m_visited.contains(path)) {
        m_visited.insert(path);
        m_queue.append(path);
    }
}

void MatchSearcher::pumpListings()
{
    if (m_inPump) {
        return; // a synchronous failure re-entered us; the outer loop continues
    }
    m_inPump = true;
    while (m_running && m_inFlight.size() < kMaxListsInFlight && !m_queue.isEmpty()) {
        const QString path = m_queue.takeFirst();
        m_inFlight.insert(path);
        m_session->listDirectory(path); // may fail synchronously
    }
    m_inPump = false;
    maybeFinish();
}

void MatchSearcher::maybeFinish()
{
    if (!m_running || m_inPump || !m_queue.isEmpty() || !m_inFlight.isEmpty()) {
        return;
    }
    m_running = false;
    computeMatches();
}

void MatchSearcher::onDirectoryListed(const QString &path, const QList<FileEntry> &entries)
{
    if (!m_running || !m_inFlight.remove(path)) {
        return; // some other consumer's listing, or ours after a cancel
    }
    ++m_foldersListed;

    const quint8 mask = sideMaskOf(path);
    for (const FileEntry &entry : entries) {
        const QString entryPath = pathutil::join(path, entry.name);
        if (entry.isDir) {
            if (sideMaskOf(entryPath) != 0) {
                enqueueDir(entryPath);
            }
        } else if (entry.size >= m_options.sizeMinBytes
                   && !matchpairing::isTmpName(entry.name)) {
            m_files.append({entryPath, entry.size, entry.inode, mask});
        }
    }

    emit progress(m_foldersListed, m_queue.size() + m_inFlight.size(), m_files.size());
    pumpListings();
}

void MatchSearcher::onDirectoryListFailed(const QString &path, const QString &message)
{
    if (!m_running || !m_inFlight.remove(path)) {
        return;
    }
    if (path == m_options.primaryPath || path == m_options.secondaryPath) {
        // A root that can't be listed means the search as a whole is invalid.
        m_running = false;
        resetState();
        emit failed(tr("Could not list %1: %2").arg(path, message));
        return;
    }
    // One unlistable subfolder (permissions, ...) shouldn't kill the search.
    ++m_folderErrors;
    emit progress(m_foldersListed, m_queue.size() + m_inFlight.size(), m_files.size());
    pumpListings();
}

void MatchSearcher::computeMatches()
{
    // Take everything the traversal gathered so the searcher is idle (and
    // restartable, even from a finished handler) before the signal goes out.
    QList<matchpairing::FileRecord> files = std::move(m_files);
    const int folderErrors = m_folderErrors;
    resetState();

    const matchpairing::Result result =
        matchpairing::computeMatches(std::move(files), m_options.sizeDiffBytes);
    emit finished(result.matches, folderErrors, result.truncated, /*cancelled*/ false);
}

void MatchSearcher::resetState()
{
    m_visited.clear();
    m_queue.clear();
    m_inFlight.clear();
    m_files.clear();
    m_foldersListed = 0;
    m_folderErrors = 0;
}
