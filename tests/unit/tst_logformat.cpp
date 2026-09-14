#include <QtTest>

#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimeZone>

#include "core/LogFormat.h"

#include "common/TestMain.h"

// The audit log's jsonl line format: level/time/msg required, structured extra
// fields allowed.
class TestLogFormat : public QObject
{
    Q_OBJECT

private slots:
    void requiredKeys();
    void timeIsRfc3339Utc();
    void localTimeIsConvertedToUtc();
    void fieldsAreMerged();
    void fieldsCannotOverrideRequiredKeys();
    void toLineIsOneCompactJsonObject();
    void awkwardStringsStayOnOneLine();

private:
    // 2026-07-29T17:22:18.808Z; nonzero milliseconds exercise the fraction.
    const QDateTime m_time{QDate(2026, 7, 29), QTime(17, 22, 18, 808),
                           QTimeZone::UTC};
};

void TestLogFormat::requiredKeys()
{
    const QJsonObject entry =
        logformat::makeEntry("info", "connected", {}, m_time);
    QCOMPARE(entry.size(), 3);
    QCOMPARE(entry.value("level").toString(), "info");
    QCOMPARE(entry.value("time").toString(), "2026-07-29T17:22:18.808Z");
    QCOMPARE(entry.value("msg").toString(), "connected");
}

void TestLogFormat::timeIsRfc3339Utc()
{
    const QJsonObject entry = logformat::makeEntry("error", "x", {}, m_time);
    const QRegularExpression rfc3339(
        QStringLiteral(R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}Z$)"));
    QVERIFY2(rfc3339.match(entry.value("time").toString()).hasMatch(),
             qPrintable(entry.value("time").toString()));
}

void TestLogFormat::localTimeIsConvertedToUtc()
{
    // A local-time QDateTime must come out as the equivalent UTC instant.
    const QDateTime local = m_time.toLocalTime();
    const QJsonObject entry = logformat::makeEntry("info", "x", {}, local);
    QCOMPARE(entry.value("time").toString(), "2026-07-29T17:22:18.808Z");
}

void TestLogFormat::fieldsAreMerged()
{
    const QJsonObject entry = logformat::makeEntry(
        "info", "file renamed",
        {{"url", "smb://u@h/s"}, {"from", "/a"}, {"to", "/b"}}, m_time);
    QCOMPARE(entry.size(), 6);
    QCOMPARE(entry.value("url").toString(), "smb://u@h/s");
    QCOMPARE(entry.value("from").toString(), "/a");
    QCOMPARE(entry.value("to").toString(), "/b");
}

void TestLogFormat::fieldsCannotOverrideRequiredKeys()
{
    const QJsonObject entry = logformat::makeEntry(
        "info", "real msg",
        {{"level", "bogus"}, {"time", "bogus"}, {"msg", "bogus"}}, m_time);
    QCOMPARE(entry.size(), 3);
    QCOMPARE(entry.value("level").toString(), "info");
    QCOMPARE(entry.value("time").toString(), "2026-07-29T17:22:18.808Z");
    QCOMPARE(entry.value("msg").toString(), "real msg");
}

void TestLogFormat::toLineIsOneCompactJsonObject()
{
    const QByteArray line = logformat::toLine(
        logformat::makeEntry("info", "connected", {{"url", "smb://u@h/s"}}, m_time));
    QVERIFY(line.endsWith('\n'));
    QCOMPARE(line.count('\n'), 1);

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QCOMPARE(doc.object().value("msg").toString(), "connected");
}

void TestLogFormat::awkwardStringsStayOnOneLine()
{
    const QString awkward = QStringLiteral("quote \" backslash \\ newline \n é");
    const QByteArray line = logformat::toLine(
        logformat::makeEntry("error", awkward, {{"detail", awkward}}, m_time));
    QCOMPARE(line.count('\n'), 1); // the embedded newline must be escaped

    const QJsonObject roundTrip = QJsonDocument::fromJson(line).object();
    QCOMPARE(roundTrip.value("msg").toString(), awkward);
    QCOMPARE(roundTrip.value("detail").toString(), awkward);
}

SMBMGR_TEST_MAIN(TestLogFormat)

#include "tst_logformat.moc"
