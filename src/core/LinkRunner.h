#pragma once

#include <QList>
#include <QObject>
#include <QString>

class SmbSession;

// Replaces files with hard links, one job at a time. Per job the sequence is
//     rename victim  -> victim.hlmgr-tmp
//     link   primary -> victim's original path
//     unlink victim.hlmgr-tmp
// so the victim's data still exists (under the tmp name) until the link is in
// place; a failed link renames the tmp back. Progress and outcomes are
// reported per job index (MatchFinderPanel maps them to its results rows).
class LinkRunner : public QObject
{
    Q_OBJECT

public:
    struct Job
    {
        QString primaryPath; // file to keep (link target)
        QString victimPath;  // file to replace with a hard link
    };

    explicit LinkRunner(SmbSession *session, QObject *parent = nullptr);

    bool isRunning() const { return !m_jobs.isEmpty(); }

    // Starts the given jobs; no-op while a run is in progress.
    void start(const QList<Job> &jobs);

signals:
    void jobStatusChanged(int jobIndex, const QString &text);
    void jobFinished(int jobIndex, bool success, const QString &message);
    void allFinished(int succeeded, int failed);

private:
    enum class Step { Rename, Link, Unlink, UndoRename };

    void startNextJob();
    void finishJob(bool success, const QString &message);
    void onOpSucceeded(quint64 id);
    void onOpFailed(quint64 id, const QString &message);

    static QString tmpPath(const Job &job);

    SmbSession *m_session = nullptr;
    QList<Job> m_jobs;
    int m_current = -1;      // index into m_jobs while running
    Step m_step = Step::Rename;
    quint64 m_opId = 0;      // id of the operation we are waiting for
    QString m_linkFailureText; // set on link failure; finalized after rename-back
    int m_succeeded = 0;
    int m_failed = 0;
};
