#include <QtTest>

#include "core/PathUtil.h"

#include "common/TestMain.h"

class TestPathUtil : public QObject
{
    Q_OBJECT

private slots:
    void normalize_data();
    void normalize();
    void join_data();
    void join();
    void folderOf_data();
    void folderOf();
    void nameOf_data();
    void nameOf();
};

void TestPathUtil::normalize_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QTest::newRow("empty")              << ""                  << "/";
    QTest::newRow("root")               << "/"                 << "/";
    QTest::newRow("simple")             << "/foo/bar"          << "/foo/bar";
    QTest::newRow("no leading slash")   << "foo/bar"           << "/foo/bar";
    QTest::newRow("trailing slash")     << "/foo/"             << "/foo";
    QTest::newRow("double slashes")     << "//foo//bar"        << "/foo/bar";
    QTest::newRow("dot segment")        << "/foo/./bar"        << "/foo/bar";
    QTest::newRow("dotdot to parent")   << "/foo/bar/.."       << "/foo";
    QTest::newRow("dotdot to root")     << "/foo/.."           << "/";
    QTest::newRow("dotdot past root")   << "/.."               << "/";
    QTest::newRow("dotdot chain past root") << "/../../foo"    << "/foo";
    QTest::newRow("only slashes")       << "//"                << "/";
    QTest::newRow("surrounding spaces") << "  /foo  "          << "/foo";
    QTest::newRow("mixed mess")         << " foo//./bar/../ "  << "/foo";
}

void TestPathUtil::normalize()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(pathutil::normalize(input), expected);
}

void TestPathUtil::join_data()
{
    QTest::addColumn<QString>("folder");
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("expected");

    QTest::newRow("at root")    << "/"        << "file.txt" << "/file.txt";
    QTest::newRow("one deep")   << "/dir"     << "file.txt" << "/dir/file.txt";
    QTest::newRow("two deep")   << "/a/b"     << "c"        << "/a/b/c";
}

void TestPathUtil::join()
{
    QFETCH(QString, folder);
    QFETCH(QString, name);
    QFETCH(QString, expected);
    QCOMPARE(pathutil::join(folder, name), expected);
}

void TestPathUtil::folderOf_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("expected");

    QTest::newRow("file at root")   << "/file.txt"     << "/";
    QTest::newRow("one deep")       << "/dir/file.txt" << "/dir";
    QTest::newRow("two deep")       << "/a/b/c"        << "/a/b";
    QTest::newRow("root itself")    << "/"             << "/";
}

void TestPathUtil::folderOf()
{
    QFETCH(QString, path);
    QFETCH(QString, expected);
    QCOMPARE(pathutil::folderOf(path), expected);
}

void TestPathUtil::nameOf_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("expected");

    QTest::newRow("file at root") << "/file.txt"     << "file.txt";
    QTest::newRow("one deep")     << "/dir/file.txt" << "file.txt";
    QTest::newRow("root itself")  << "/"             << "";
}

void TestPathUtil::nameOf()
{
    QFETCH(QString, path);
    QFETCH(QString, expected);
    QCOMPARE(pathutil::nameOf(path), expected);
}

SMBMGR_TEST_MAIN(TestPathUtil)

#include "tst_pathutil.moc"
