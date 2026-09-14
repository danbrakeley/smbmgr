#include <QtTest>

#include <QJsonDocument>
#include <QTemporaryDir>

#include "core/Logger.h"

#include "common/TestMain.h"

class TestLogger : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();
    void writesJsonlLines();
    void appendsAcrossReopen();
    void createsMissingDirectories();
    void emitsEntryLoggedWithoutFile();

private:
    // Parses every line of a .jsonl file, failing the test on invalid JSON.
    QList<QJsonObject> readLog(const QString &path);
};

void TestLogger::cleanup()
{
    // Logger is a singleton: leave no file target behind for other tests.
    Logger::instance().setFilePath(QString());
}

QList<QJsonObject> TestLogger::readLog(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QList<QJsonObject> entries;
    for (const QByteArray &line : file.readAll().split('\n')) {
        if (line.isEmpty()) {
            continue;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            return {};
        }
        entries.append(doc.object());
    }
    return entries;
}

void TestLogger::writesJsonlLines()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("log.jsonl");
    Logger::instance().setFilePath(path);

    Logger::instance().info("connected", {{"url", "smb://u@h/s"}});
    Logger::instance().error("rename failed", {{"from", "/a"}, {"error", "boom"}});

    const QList<QJsonObject> entries = readLog(path);
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries[0].value("level").toString(), "info");
    QCOMPARE(entries[0].value("msg").toString(), "connected");
    QCOMPARE(entries[0].value("url").toString(), "smb://u@h/s");
    QVERIFY(entries[0].contains("time"));
    QCOMPARE(entries[1].value("level").toString(), "error");
    QCOMPARE(entries[1].value("msg").toString(), "rename failed");
    QCOMPARE(entries[1].value("from").toString(), "/a");
    QCOMPARE(entries[1].value("error").toString(), "boom");
}

void TestLogger::appendsAcrossReopen()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("log.jsonl");

    Logger::instance().setFilePath(path);
    Logger::instance().info("first");
    // A new session appends to the existing history rather than truncating it.
    Logger::instance().setFilePath(path);
    Logger::instance().info("second");

    const QList<QJsonObject> entries = readLog(path);
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries[0].value("msg").toString(), "first");
    QCOMPARE(entries[1].value("msg").toString(), "second");
}

void TestLogger::createsMissingDirectories()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("a/b/log.jsonl");
    Logger::instance().setFilePath(path);
    Logger::instance().info("hello");
    QCOMPARE(readLog(path).size(), 1);
}

void TestLogger::emitsEntryLoggedWithoutFile()
{
    Logger::instance().setFilePath(QString());
    QSignalSpy spy(&Logger::instance(), &Logger::entryLogged);
    Logger::instance().info("viewer hook", {{"n", 3}});
    QCOMPARE(spy.count(), 1);
    const QJsonObject entry = spy.first().first().toJsonObject();
    QCOMPARE(entry.value("msg").toString(), "viewer hook");
    QCOMPARE(entry.value("n").toInt(), 3);
}

SMBMGR_TEST_MAIN(TestLogger)

#include "tst_logger.moc"
