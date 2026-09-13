#pragma once

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>

#include "smb/SmbTypes.h"

#include <optional>

class QHostInfo;
class QJsonObject;
class QSocketNotifier;
class QTimer;
class QUrl;

struct smb2_context;

// The parsed pieces of a `smb://[domain;]user@host[:port]/share` URL.
struct SmbShareSpec
{
    QString host;
    int port = -1;   // -1 means the SMB default (445)
    QString share;   // share name (no slashes)
    QString user;
    QString domain;  // optional workgroup/domain (from the "domain;user" URL syntax)

    // "host", "host:port", or "[v6addr]:port" — the form libsmb2 expects.
    QString hostPort() const
    {
        QString h = host.contains(QLatin1Char(':'))
                        ? QLatin1Char('[') + host + QLatin1Char(']')
                        : host;
        return port != -1 ? h + QLatin1Char(':') + QString::number(port) : h;
    }

    QString displayName() const
    {
        return user + QLatin1Char('@') + hostPort() + QLatin1Char('/') + share;
    }

    // The connection's identity for the audit log, never with the password:
    // "smb://[domain;]user@host[:port]/share".
    QString url() const
    {
        const QString auth =
            domain.isEmpty() ? user : domain + QLatin1Char(';') + user;
        return QStringLiteral("smb://") + auth + QLatin1Char('@') + hostPort()
               + QLatin1Char('/') + share;
    }

    // Returns std::nullopt and fills *errorMessage if the URL is not a valid
    // share-root SMB URL.
    static std::optional<SmbShareSpec> fromUrl(const QUrl &url, QString *errorMessage);
};

// Asynchronous wrapper around a single libsmb2 connection.
//
// Runs entirely on the GUI thread: libsmb2's async API is driven by
// QSocketNotifiers on the context's socket plus a coarse tick timer for its
// timeout processing, so the rest of the app needs no locking or cross-thread
// marshalling. This is also what makes the connect flow abortable and lets
// many stat requests be in flight at once.
class SmbSession : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Disconnected,
        Connecting,
        Connected,
    };
    Q_ENUM(State)

    explicit SmbSession(QObject *parent = nullptr);
    ~SmbSession() override;

    State state() const { return m_state; }

    // Begins connecting. Progress is reported via stateChanged(); a failed
    // attempt emits stateChanged(Disconnected) followed by errorOccurred().
    // No-op unless currently Disconnected.
    void connectToShare(const SmbShareSpec &spec, const QString &password);

    // Cancels an in-progress connection attempt (Connecting -> Disconnected).
    void abortConnect();

    // Drops an established connection (Connected -> Disconnected).
    void disconnectFromShare();

    // Requests a listing of a share-absolute path ("/" is the share root).
    // Answered by directoryListed() or directoryListFailed() with the same
    // path; replies for concurrent requests can arrive in any order.
    void listDirectory(const QString &path);

    // Requests a per-file stat (the only way to learn the hard-link count —
    // enumeration never carries it). Answered by fileStatted() or
    // statFailed(); many stats can be in flight at once (pipelined PDUs).
    void statFile(const QString &path);

    // Mutating operations, used by LinkRunner to chain its rename -> link ->
    // unlink sequence. Each returns a request id; completion arrives as
    // operationSucceeded(id) or operationFailed(id, message) — always
    // asynchronously, even for immediate failures, so callers can store the
    // id before its completion arrives.
    quint64 renameFile(const QString &fromPath, const QString &toPath);
    quint64 createHardLink(const QString &existingPath, const QString &newLinkPath);
    quint64 removeFile(const QString &path);

signals:
    void stateChanged(SmbSession::State state);
    void errorOccurred(const QString &message);
    void directoryListed(const QString &path, const QList<FileEntry> &entries);
    void directoryListFailed(const QString &path, const QString &message);
    void fileStatted(const QString &path, int nlink, quint64 inode);
    void statFailed(const QString &path, const QString &message);
    void operationSucceeded(quint64 id);
    void operationFailed(quint64 id, const QString &message);

private:
    struct ListRequest;
    struct StatRequest;
    struct OpRequest;

    static void onConnectDone(struct smb2_context *ctx, int status,
                              void *commandData, void *privateData);
    static void onOpendirDone(struct smb2_context *ctx, int status,
                              void *commandData, void *privateData);
    static void onStatDone(struct smb2_context *ctx, int status,
                           void *commandData, void *privateData);
    static void onOpDone(struct smb2_context *ctx, int status,
                         void *commandData, void *privateData);

    void onHostResolved(const QHostInfo &info);
    void startSmbConnect(const QString &server);
    void statFileNow(const QString &path);
    // Allocates an id, logs the failure, and queues its operationFailed().
    quint64 failOpLater(const QString &failMsg, const QJsonObject &fields,
                        const QString &message);
    OpRequest *newOpRequest(const QString &okMsg, const QString &failMsg,
                            const QJsonObject &fields);
    void service(int revents);
    void syncNotifiersToContext();
    void teardown();
    void setState(State state);

    SmbShareSpec m_spec;    // spec of the current/last connection attempt
    QString m_password;     // held only between connectToShare() and startSmbConnect()
    int m_lookupId = -1;    // pending QHostInfo lookup, -1 when none
    struct smb2_context *m_ctx = nullptr;
    QSocketNotifier *m_readNotifier = nullptr;
    QSocketNotifier *m_writeNotifier = nullptr;
    QTimer *m_tickTimer = nullptr;
    qintptr m_fd = -1;
    State m_state = State::Disconnected;
    int m_statDelayMs = 0; // debug throttle, see HLM_STAT_DELAY_MS
    bool m_teardownPending = false;
    bool m_inTeardown = false;
    bool m_inService = false; // inside smb2_service(); teardown must defer
    QString m_pendingError;
    QSet<ListRequest *> m_pendingLists;
    QSet<StatRequest *> m_pendingStats;
    QSet<OpRequest *> m_pendingOps;
    quint64 m_nextOpId = 1;
};
