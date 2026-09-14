#pragma once

#include <QString>
#include <QStringList>

#include <QtTest>

class SmbSession;

// Access to the Samba docker fixture (tests/docker/): connection parameters
// come from env vars (defaults match docker-compose.yml), and share state is
// seeded/verified out-of-band with `docker exec` — SmbSession deliberately
// has no file-creation API, and the container-side `stat` is the ground truth
// the SMB results are compared against.
//
//   SMBMGR_TEST_SMB_URL        default smb://smbmgrtest@localhost:10445/share
//   SMBMGR_TEST_SMB_PASSWORD   default smbmgrtest
//   SMBMGR_TEST_SMB_CONTAINER  default smbmgr-test-samba
//   SMBMGR_TEST_SMB_STRICT     "1": fail instead of skip when the server is down
//
// Every fixture instance namespaces its files under a unique run directory,
// so suites are order-independent and safe under `ctest -j`; the volume is
// wiped when ctest tears the fixture down (`docker compose down -v`).
class SmbFixture
{
public:
    SmbFixture();

    QString url() const { return m_url; }
    QString password() const { return m_password; }
    bool strict() const;

    // Starts a connect and pumps the event loop until the session leaves
    // Connecting; true once it reports Connected.
    bool connectForTest(SmbSession &session, int timeoutMs = 15000);

    // --- out-of-band share manipulation; paths are share-absolute ----------

    // Creates (and returns) this run's unique directory for one test case,
    // e.g. "/smbmgr-1a2b3c4d/rename". Nested names ("list/sub") are allowed.
    QString makeCaseDir(const QString &caseName);
    void makeDir(const QString &sharePath);
    void seedFile(const QString &shareDir, const QString &name, qint64 size,
                  char fill = 'a');
    void seedManyFiles(const QString &shareDir, int count, qint64 size); // f1.bin..fN.bin
    // d1..dN subdirectories, each holding one file of the given size.
    void seedManyDirs(const QString &shareDir, int dirCount, qint64 fileSize);
    void makeHardLink(const QString &existingPath, const QString &newPath);
    void chmodPath(const QString &sharePath, const QString &mode);

    struct Stat
    {
        int nlink = -1;
        quint64 inode = 0;
    };
    Stat statPath(const QString &sharePath);
    bool exists(const QString &sharePath);
    QStringList ls(const QString &shareDir);
    QString readFile(const QString &sharePath);

private:
    QString containerPath(const QString &sharePath) const;
    QString quoted(const QString &sharePath) const;
    bool shell(const QString &script, QString *stdOut = nullptr);

    QString m_url;
    QString m_password;
    QString m_container;
    QString m_runRoot; // share-absolute, unique per fixture instance
};

// QSKIP/QFAIL only work inside the running test function, hence a macro. Use
// in initTestCase() to skip (or, under SMBMGR_TEST_SMB_STRICT=1, fail) the whole
// suite when the fixture isn't up.
#define SMBMGR_CONNECT_OR_SKIP(fixture, session)                                \
    do {                                                                        \
        if (!(fixture).connectForTest(session)) {                               \
            if ((fixture).strict()) {                                           \
                QFAIL("SMB test server unreachable (SMBMGR_TEST_SMB_STRICT=1)"); \
            } else {                                                            \
                QSKIP("SMB test server unreachable — is the Samba fixture up? " \
                      "(docker compose -f tests/docker/docker-compose.yml "     \
                      "up -d --wait)");                                         \
            }                                                                   \
        }                                                                       \
    } while (false)
