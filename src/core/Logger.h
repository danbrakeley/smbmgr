#pragma once

#include <QFile>
#include <QJsonObject>
#include <QObject>

// App-wide audit log: appends one compact JSON object per line to a .jsonl
// file. Every action that changes files/folders on the server must be logged,
// plus connects, disconnects, and network errors — all of which flow through
// SmbSession, so that's where the log calls live.
//
// File output is off until setFilePath() points somewhere (main() sets the
// app-data location; tests use a temp dir or leave it off). entryLogged fires
// either way.
class Logger : public QObject
{
    Q_OBJECT

public:
    static Logger &instance();

    // Appends to the given file from now on (creating missing directories);
    // an empty path disables file output.
    void setFilePath(const QString &path);
    QString filePath() const { return m_file.fileName(); }

    // "info" = expected, "error" = unexpected; there is deliberately no warn.
    void info(const QString &msg, const QJsonObject &fields = {});
    void error(const QString &msg, const QJsonObject &fields = {});

signals:
    void entryLogged(const QJsonObject &entry);

private:
    Logger() = default;
    void log(const QString &level, const QString &msg, const QJsonObject &fields);

    QFile m_file;
};
