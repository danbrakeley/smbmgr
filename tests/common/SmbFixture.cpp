#include "SmbFixture.h"

#include <QElapsedTimer>
#include <QProcess>
#include <QUrl>
#include <QUuid>

#include "smb/SmbSession.h"

namespace {

QString envOr(const char *name, const QString &fallback)
{
    const QByteArray value = qgetenv(name);
    return value.isEmpty() ? fallback : QString::fromUtf8(value);
}

} // namespace

SmbFixture::SmbFixture()
    : m_url(envOr("SMBMGR_TEST_SMB_URL", "smb://smbmgrtest@localhost:10445/share"))
    , m_password(envOr("SMBMGR_TEST_SMB_PASSWORD", "smbmgrtest"))
    , m_container(envOr("SMBMGR_TEST_SMB_CONTAINER", "smbmgr-test-samba"))
    , m_runRoot("/smbmgr-" + QUuid::createUuid().toString(QUuid::Id128).left(12))
{
}

bool SmbFixture::strict() const
{
    return qgetenv("SMBMGR_TEST_SMB_STRICT") == "1";
}

bool SmbFixture::connectForTest(SmbSession &session, int timeoutMs)
{
    QString error;
    const auto spec = SmbShareSpec::fromUrl(QUrl(m_url), &error);
    if (!spec) {
        qWarning("SmbFixture: bad SMBMGR_TEST_SMB_URL %s: %s",
                 qPrintable(m_url), qPrintable(error));
        return false;
    }
    session.connectToShare(*spec, m_password);
    QElapsedTimer timer;
    timer.start();
    while (session.state() == SmbSession::State::Connecting
           && timer.elapsed() < timeoutMs) {
        QTest::qWait(25);
    }
    return session.state() == SmbSession::State::Connected;
}

QString SmbFixture::containerPath(const QString &sharePath) const
{
    return sharePath == QLatin1String("/") ? QStringLiteral("/srv/share")
                                           : QStringLiteral("/srv/share") + sharePath;
}

// Share paths in these tests are ASCII without quotes; single-quoting keeps
// the shell out of trouble anyway.
QString SmbFixture::quoted(const QString &sharePath) const
{
    return QLatin1Char('\'') + containerPath(sharePath) + QLatin1Char('\'');
}

bool SmbFixture::shell(const QString &script, QString *stdOut)
{
    QProcess proc;
    proc.start("docker", {"exec", m_container, "sh", "-c", script});
    if (!proc.waitForFinished(30000)) {
        qWarning("SmbFixture: docker exec timed out: %s", qPrintable(script));
        return false;
    }
    if (stdOut) {
        *stdOut = QString::fromUtf8(proc.readAllStandardOutput());
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        qWarning("SmbFixture: docker exec failed (%d): %s\n%s", proc.exitCode(),
                 qPrintable(script),
                 proc.readAllStandardError().constData());
        return false;
    }
    return true;
}

QString SmbFixture::makeCaseDir(const QString &caseName)
{
    const QString dir = m_runRoot + QLatin1Char('/') + caseName;
    makeDir(dir);
    return dir;
}

void SmbFixture::makeDir(const QString &sharePath)
{
    // chown -R from the run root: mkdir -p may have created root-owned
    // parents, and the SMB user needs directory write permission for
    // create/rename/unlink.
    shell(QStringLiteral("mkdir -p %1 && chown -R smbmgrtest:smbmgrtest %2")
              .arg(quoted(sharePath), quoted(m_runRoot)));
}

void SmbFixture::seedFile(const QString &shareDir, const QString &name,
                          qint64 size, char fill)
{
    const QString path = shareDir + QLatin1Char('/') + name;
    shell(QStringLiteral("head -c %1 /dev/zero | tr '\\0' '%2' > %3 "
                         "&& chown smbmgrtest:smbmgrtest %3")
              .arg(size)
              .arg(QLatin1Char(fill))
              .arg(quoted(path)));
}

void SmbFixture::seedManyFiles(const QString &shareDir, int count, qint64 size)
{
    shell(QStringLiteral("cd %1 && for i in $(seq 1 %2); do "
                         "head -c %3 /dev/zero | tr '\\0' 'a' > f$i.bin; done "
                         "&& chown -R smbmgrtest:smbmgrtest .")
              .arg(quoted(shareDir))
              .arg(count)
              .arg(size));
}

void SmbFixture::seedManyDirs(const QString &shareDir, int dirCount, qint64 fileSize)
{
    shell(QStringLiteral("cd %1 && for i in $(seq 1 %2); do mkdir -p d$i; "
                         "head -c %3 /dev/zero | tr '\\0' 'a' > d$i/f.bin; done "
                         "&& chown -R smbmgrtest:smbmgrtest .")
              .arg(quoted(shareDir))
              .arg(dirCount)
              .arg(fileSize));
}

void SmbFixture::makeHardLink(const QString &existingPath, const QString &newPath)
{
    shell(QStringLiteral("ln %1 %2").arg(quoted(existingPath), quoted(newPath)));
}

void SmbFixture::chmodPath(const QString &sharePath, const QString &mode)
{
    shell(QStringLiteral("chmod %1 %2").arg(mode, quoted(sharePath)));
}

SmbFixture::Stat SmbFixture::statPath(const QString &sharePath)
{
    QString out;
    Stat result;
    if (shell(QStringLiteral("stat -c '%h %i' ") + quoted(sharePath), &out)) {
        const QStringList parts = out.trimmed().split(QLatin1Char(' '));
        if (parts.size() == 2) {
            result.nlink = parts.at(0).toInt();
            result.inode = parts.at(1).toULongLong();
        }
    }
    return result;
}

bool SmbFixture::exists(const QString &sharePath)
{
    QProcess proc;
    proc.start("docker", {"exec", m_container, "test", "-e", containerPath(sharePath)});
    proc.waitForFinished(30000);
    return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

QStringList SmbFixture::ls(const QString &shareDir)
{
    QString out;
    if (!shell(QStringLiteral("ls -A ") + quoted(shareDir), &out)) {
        return {};
    }
    return out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

QString SmbFixture::readFile(const QString &sharePath)
{
    QString out;
    shell(QStringLiteral("cat ") + quoted(sharePath), &out);
    return out;
}
