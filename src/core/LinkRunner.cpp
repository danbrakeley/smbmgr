#include "LinkRunner.h"

#include "smb/SmbSession.h"

namespace {

const QLatin1String kTmpSuffix(".smbmgr-tmp");

} // namespace

LinkRunner::LinkRunner(SmbSession *session, QObject *parent)
    : QObject(parent)
    , m_session(session)
{
    connect(m_session, &SmbSession::operationSucceeded,
            this, &LinkRunner::onOpSucceeded);
    connect(m_session, &SmbSession::operationFailed,
            this, &LinkRunner::onOpFailed);
}

void LinkRunner::start(const QList<Job> &jobs)
{
    if (isRunning() || jobs.isEmpty()) {
        return;
    }
    m_jobs = jobs;
    m_current = -1;
    m_succeeded = 0;
    m_failed = 0;
    startNextJob();
}

QString LinkRunner::tmpPath(const Job &job)
{
    return job.victimPath + kTmpSuffix;
}

void LinkRunner::startNextJob()
{
    ++m_current;
    if (m_current >= m_jobs.size()) {
        const int succeeded = m_succeeded;
        const int failed = m_failed;
        // Clear first so isRunning() is false inside allFinished handlers.
        m_jobs.clear();
        m_current = -1;
        emit allFinished(succeeded, failed);
        return;
    }
    emit jobStatusChanged(m_current, tr("moving original aside…"));
    m_step = Step::Rename;
    const Job &job = m_jobs.at(m_current);
    m_opId = m_session->renameFile(job.victimPath, tmpPath(job));
}

void LinkRunner::finishJob(bool success, const QString &message)
{
    if (success) {
        ++m_succeeded;
    } else {
        ++m_failed;
    }
    emit jobFinished(m_current, success, message);
    startNextJob();
}

void LinkRunner::onOpSucceeded(quint64 id)
{
    if (!isRunning() || id != m_opId) {
        return;
    }
    const Job &job = m_jobs.at(m_current);

    switch (m_step) {
    case Step::Rename:
        emit jobStatusChanged(m_current, tr("creating hard link…"));
        m_step = Step::Link;
        m_opId = m_session->createHardLink(job.primaryPath, job.victimPath);
        break;
    case Step::Link:
        emit jobStatusChanged(m_current, tr("removing original…"));
        m_step = Step::Unlink;
        m_opId = m_session->removeFile(tmpPath(job));
        break;
    case Step::Unlink:
        finishJob(true, tr("replaced with hard link"));
        break;
    case Step::UndoRename:
        // The failed-link text is already in place; add the good news.
        finishJob(false, m_linkFailureText + tr(" (original restored)"));
        break;
    }
}

void LinkRunner::onOpFailed(quint64 id, const QString &message)
{
    if (!isRunning() || id != m_opId) {
        return;
    }
    const Job &job = m_jobs.at(m_current);

    switch (m_step) {
    case Step::Rename:
        finishJob(false, tr("failed: %1").arg(message));
        break;
    case Step::Link:
        // The original is intact under the tmp name; put it back.
        m_linkFailureText = tr("link failed: %1").arg(message);
        emit jobStatusChanged(m_current, m_linkFailureText);
        m_step = Step::UndoRename;
        m_opId = m_session->renameFile(tmpPath(job), job.victimPath);
        break;
    case Step::Unlink:
        // The link exists; only the cleanup of the tmp copy failed.
        finishJob(false, tr("linked, but could not remove %1: %2")
                             .arg(tmpPath(job), message));
        break;
    case Step::UndoRename:
        finishJob(false, m_linkFailureText
                             + tr(" — RESTORE FAILED, original left at %1: %2")
                                   .arg(tmpPath(job), message));
        break;
    }
}
