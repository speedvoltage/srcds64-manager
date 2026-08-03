#include "core/steamcmd_manager.h"

#include "core/account.h"
#include "core/elf_inspector.h"

#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

#include <utility>

namespace srcds64 {

namespace {

const QUrl steamCmdArchiveUrl(QStringLiteral("https://steamcdn-a.akamaihd.net/client/installer/steamcmd_linux.tar.gz"));

}

bool SteamCmdResolution::valid() const
{
    return !executable.isEmpty();
}

SteamCmdManager::SteamCmdManager(ProcessRunner &runner, UnixAccount account, QString dataRoot)
    : m_runner(runner),
      m_account(std::move(account)),
      m_dataRoot(QDir::cleanPath(std::move(dataRoot)))
{
}

SteamCmdResolution SteamCmdManager::discover(const QString &explicitPath) const
{
    QStringList diagnostics;

    for (const QString &candidate : candidatePaths(explicitPath)) {
        QString diagnostic;
        if (validate(candidate, &diagnostic)) {
            return {
                QFileInfo(candidate).absoluteFilePath(),
                QDir::cleanPath(QFileInfo(candidate).absolutePath()) == QDir::cleanPath(managedDirectory()),
                diagnostic
            };
        }

        if (!diagnostic.isEmpty()) {
            diagnostics.push_back(QStringLiteral("%1: %2").arg(candidate, diagnostic));
        }
    }

    return {{}, false, diagnostics.join(QLatin1Char('\n'))};
}

SteamCmdResolution SteamCmdManager::ensure(const QString &explicitPath, QString *error)
{
    const SteamCmdResolution existing = discover(explicitPath);
    if (existing.valid()) {
        return existing;
    }

    if (!installManaged(error)) {
        return {};
    }

    const QString managedExecutable = managedDirectory() + QStringLiteral("/steamcmd.sh");
    QString diagnostic;
    if (!validate(managedExecutable, &diagnostic)) {
        if (error) {
            *error = QStringLiteral("The managed SteamCMD installation is not usable: %1").arg(diagnostic);
        }
        return {};
    }

    return {managedExecutable, true, diagnostic};
}

ProcessResult SteamCmdManager::execute(const QString &executable, const QStringList &commands, int timeoutMs, bool forwardOutput) const
{
    return m_runner.run(
        executable,
        commands,
        QFileInfo(executable).absolutePath(),
        QProcessEnvironment::systemEnvironment(),
        timeoutMs,
        &m_account,
        forwardOutput);
}

QString SteamCmdManager::ensureSteamClient64(const SteamCmdResolution &resolution, QString *error)
{
    QString steamClient = findSteamClient64(resolution.executable);
    if (!steamClient.isEmpty()) {
        return steamClient;
    }

    const QString managedExecutable = managedDirectory() + QStringLiteral("/steamcmd.sh");
    QString diagnostic;
    if (!validate(managedExecutable, &diagnostic)) {
        if (!installManaged(error)) {
            return {};
        }
        if (!validate(managedExecutable, &diagnostic)) {
            if (error) {
                *error = QStringLiteral("The managed SteamCMD installation cannot provide its 64-bit runtime: %1").arg(diagnostic);
            }
            return {};
        }
    }

    steamClient = findSteamClient64(managedExecutable);
    if (steamClient.isEmpty()) {
        if (error) {
            *error = QStringLiteral(
                "No verified ELF64 steamclient.so was found after updating managed SteamCMD. "
                "Expected it under %1/linux64/steamclient.so.")
                         .arg(managedDirectory());
        }
        return {};
    }

    return steamClient;
}

QString SteamCmdManager::dataRoot() const
{
    return m_dataRoot;
}

QString SteamCmdManager::managedDirectory() const
{
    return m_dataRoot + QStringLiteral("/steamcmd");
}

QString SteamCmdManager::findSteamClient64(const QString &executable) const
{
    QStringList roots;
    const QFileInfo executableInfo(executable);
    if (!executable.isEmpty()) {
        roots.push_back(executableInfo.absolutePath());
        const QString canonicalExecutable = executableInfo.canonicalFilePath();
        if (!canonicalExecutable.isEmpty()) {
            roots.push_back(QFileInfo(canonicalExecutable).absolutePath());
        }
    }

    roots.append({
        managedDirectory(),
        m_account.home + QStringLiteral("/.steam/steamcmd"),
        m_account.home + QStringLiteral("/Steam"),
        m_account.home + QStringLiteral("/.local/share/Steam")
    });

    QSet<QString> checked;
    for (const QString &root : roots) {
        const QString candidate = QDir::cleanPath(root) + QStringLiteral("/linux64/steamclient.so");
        if (!checked.contains(candidate)) {
            checked.insert(candidate);
            if (ElfInspector::isElf64(candidate)) {
                return QFileInfo(candidate).absoluteFilePath();
            }
        }
    }

    const QString sdkCandidate = m_account.home + QStringLiteral("/.steam/sdk64/steamclient.so");
    if (ElfInspector::isElf64(sdkCandidate)) {
        return QFileInfo(sdkCandidate).absoluteFilePath();
    }

    return {};
}

QStringList SteamCmdManager::candidatePaths(const QString &explicitPath) const
{
    QStringList paths;
    if (!explicitPath.isEmpty()) {
        paths.push_back(QFileInfo(explicitPath).absoluteFilePath());
    }

    paths.push_back(managedDirectory() + QStringLiteral("/steamcmd.sh"));

    const QString pathSteamCmd = QStandardPaths::findExecutable(QStringLiteral("steamcmd"));
    if (!pathSteamCmd.isEmpty()) {
        paths.push_back(pathSteamCmd);
    }

    const QString pathSteamCmdScript = QStandardPaths::findExecutable(QStringLiteral("steamcmd.sh"));
    if (!pathSteamCmdScript.isEmpty()) {
        paths.push_back(pathSteamCmdScript);
    }

    paths.append({
        QStringLiteral("/usr/games/steamcmd"),
        QStringLiteral("/usr/bin/steamcmd"),
        m_account.home + QStringLiteral("/steamcmd/steamcmd.sh"),
        m_account.home + QStringLiteral("/Steam/steamcmd.sh")
    });

    QStringList uniquePaths;
    QSet<QString> seen;
    for (const QString &path : paths) {
        const QString cleanPath = QDir::cleanPath(path);
        if (!seen.contains(cleanPath)) {
            seen.insert(cleanPath);
            uniquePaths.push_back(cleanPath);
        }
    }
    return uniquePaths;
}

bool SteamCmdManager::validate(const QString &path, QString *diagnostic) const
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        if (diagnostic) {
            *diagnostic = QStringLiteral("not found");
        }
        return false;
    }

    if (!info.isExecutable()) {
        if (diagnostic) {
            *diagnostic = QStringLiteral("not executable");
        }
        return false;
    }

    const ProcessResult result = execute(path, {QStringLiteral("+quit")}, 300000, false);
    if (!result.succeeded()) {
        if (diagnostic) {
            QString detail = result.errorString;
            if (detail.isEmpty()) {
                detail = result.output.trimmed();
            }
            if (detail.isEmpty()) {
                detail = result.timedOut ? QStringLiteral("timed out") : QStringLiteral("exit code %1").arg(result.exitCode);
            }
            *diagnostic = detail;
        }
        return false;
    }

    if (diagnostic) {
        *diagnostic = QStringLiteral("validated successfully");
    }
    return true;
}

bool SteamCmdManager::installManaged(QString *error)
{
    if (!AccountResolver::ensureDirectory(m_dataRoot, m_account, error)) {
        return false;
    }

    const QString managedDir = managedDirectory();
    const QFileInfo managedInfo(managedDir);
    if (managedInfo.exists()) {
        const QString backup = managedDir + QStringLiteral(".broken-") + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        if (!QDir().rename(managedDir, backup)) {
            if (error) {
                *error = QStringLiteral("Unable to preserve the broken managed SteamCMD directory: %1").arg(managedDir);
            }
            return false;
        }
    }

    if (!AccountResolver::ensureDirectory(managedDir, m_account, error)) {
        return false;
    }

    const QString archivePath = m_dataRoot + QStringLiteral("/steamcmd_linux.tar.gz");
    if (!downloadArchive(archivePath, error)) {
        return false;
    }

    if (!AccountResolver::applyOwnership(archivePath, m_account, false, error)) {
        QFile::remove(archivePath);
        return false;
    }

    QString tar = QStandardPaths::findExecutable(QStringLiteral("tar"));
    if (tar.isEmpty()) {
        tar = QStringLiteral("/usr/bin/tar");
    }

    const ProcessResult extraction = m_runner.run(
        tar,
        {QStringLiteral("-xzf"), archivePath, QStringLiteral("-C"), managedDir},
        m_dataRoot,
        QProcessEnvironment::systemEnvironment(),
        120000,
        &m_account,
        true);

    QFile::remove(archivePath);

    if (!extraction.succeeded()) {
        if (error) {
            *error = QStringLiteral("Unable to extract SteamCMD: %1").arg(extraction.errorString.isEmpty() ? extraction.output.trimmed() : extraction.errorString);
        }
        return false;
    }

    const QString executable = managedDir + QStringLiteral("/steamcmd.sh");
    QFile::setPermissions(executable, QFile::permissions(executable) | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
    return AccountResolver::applyOwnership(managedDir, m_account, true, error);
}

bool SteamCmdManager::downloadArchive(const QString &destination, QString *error) const
{
    QNetworkAccessManager manager;
    QNetworkRequest request(steamCmdArchiveUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("srcds64-manager/%1").arg(QStringLiteral(SRCDS64_MANAGER_VERSION)));

    QNetworkReply *reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        if (error) {
            *error = QStringLiteral("Unable to download SteamCMD: %1").arg(reply->errorString());
        }
        reply->deleteLater();
        return false;
    }

    const QByteArray archive = reply->readAll();
    reply->deleteLater();

    if (archive.isEmpty()) {
        if (error) {
            *error = QStringLiteral("The SteamCMD download was empty.");
        }
        return false;
    }

    QSaveFile file(destination);
    if (!file.open(QIODevice::WriteOnly) || file.write(archive) != archive.size() || !file.commit()) {
        if (error) {
            *error = QStringLiteral("Unable to save the SteamCMD archive: %1").arg(destination);
        }
        return false;
    }

    return true;
}

}
