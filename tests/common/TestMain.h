#pragma once

// Shared main() for every test executable, in place of QTEST_MAIN. It
//  - defaults QT_QPA_PLATFORM to "offscreen" so suites run headless (export
//    a different value first to watch a widget test on screen),
//  - initializes Winsock on Windows (libsmb2 leaves that to the application,
//    same as src/main.cpp),
//  - registers smbmgr_core's Qt resources (Q_INIT_RESOURCE — see the
//    static-library note in the top-level CMakeLists.txt),
//  - points QSettings/QStandardPaths at a test location so suites never touch
//    the developer's real smbmgr configuration.

#include <QApplication>
#include <QStandardPaths>
#include <QtTest>

#ifdef Q_OS_WIN
#include <winsock2.h>

#include <crtdbg.h>
#include <stdlib.h>
// A qFatal/abort in a debug build pops a CRT dialog that silently hangs a
// headless ctest run; report to stderr and die instead.
#define SMBMGR_WIN_INIT                                         \
    do {                                                        \
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT); \
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);       \
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);     \
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);      \
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);    \
        WSADATA wsaData;                                        \
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {        \
            return 1;                                           \
        }                                                       \
    } while (false);
#else
#define SMBMGR_WIN_INIT
#endif

#define SMBMGR_TEST_MAIN(TestClass)                                                  \
    int main(int argc, char *argv[])                                                 \
    {                                                                                 \
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {                         \
            qputenv("QT_QPA_PLATFORM", "offscreen");                                  \
        }                                                                             \
        SMBMGR_WIN_INIT                                                               \
        Q_INIT_RESOURCE(icons);                                                       \
        QApplication app(argc, argv);                                                 \
        QCoreApplication::setOrganizationName(QStringLiteral("brakeley"));            \
        QCoreApplication::setApplicationName(QStringLiteral("smbmgr-test"));          \
        QStandardPaths::setTestModeEnabled(true);                                     \
        TestClass tc;                                                                 \
        QTEST_SET_MAIN_SOURCE_PATH                                                    \
        return QTest::qExec(&tc, argc, argv);                                         \
    }
