#pragma once

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

// Builds the audit-log lines: jsonl, where every entry carries `level` ("info"
// or "error" — no warnings), `time` (RFC 3339 UTC with milliseconds), and
// `msg`, plus message-specific structured fields.
namespace logformat {

inline QJsonObject makeEntry(const QString &level, const QString &msg,
                             const QJsonObject &fields, const QDateTime &time)
{
    QJsonObject entry = fields;
    // Inserted after the copy so a stray field can't override the required keys.
    entry.insert(QStringLiteral("level"), level);
    entry.insert(QStringLiteral("time"), time.toUTC().toString(Qt::ISODateWithMs));
    entry.insert(QStringLiteral("msg"), msg);
    return entry;
}

// One compact JSON object per line; QJson escapes any embedded newlines.
inline QByteArray toLine(const QJsonObject &entry)
{
    return QJsonDocument(entry).toJson(QJsonDocument::Compact) + '\n';
}

} // namespace logformat
