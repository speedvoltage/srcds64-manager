#include "core/depot_downloader_manager.h"

#include "core/account.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace srcds64 {

namespace {

const QString releaseTag = QStringLiteral("DepotDownloader_3.4.0");
const QString releaseMetadataUrl = QStringLiteral("https://api.github.com/repos/SteamRE/DepotDownloader/releases/tags/DepotDownloader_3.4.0");
const QString releaseAssetName = QStringLiteral("DepotDownloader-linux-x64.zip");
const QString releaseAssetUrl = QStringLiteral("https://github.com/SteamRE/DepotDownloader/releases/download/DepotDownloader_3.4.0/DepotDownloader-linux-x64.zip");

}

bool DepotDownloaderResolution::valid() const
{
    return !executable.isEmpty();
}

DepotDownloaderManager::DepotDownloaderManager(ProcessRunner &runner, UnixAccount account, QString dataRoot)
    : m_runner(runner),
      m_account(std::move(account)),
      m_dataRoot(QDir::cleanPath(std::move(dataRoot)))
{
}

DepotDownloaderResolution DepotDownloaderManager::discover(const QString &explicitPath) const
{
    DepotDownloaderResolution resolution;
    QStringList diagnostics;

    for (const QString &path : candidatePaths(explicitPath)) {
        QString version;
        QString diagnostic;
        if (validate(path, &version, &diagnostic)) {
            resolution.executable = path;
            resolution.version = version;
            resolution.managed = QDir::cleanPath(path) == QDir::cleanPath(managedDirectory() + QStringLiteral("/DepotDownloader"));
            resolution.diagnostic = diagnostic;
            return resolution;
        }
        diagnostics.push_back(QStringLiteral("%1: %2").arg(path, diagnostic));
    }

    resolution.diagnostic = diagnostics.join(QLatin1Char('\n'));
    return resolution;
}

DepotDownloaderResolution DepotDownloaderManager::ensure(const QString &explicitPath, QString *error)
{
    DepotDownloaderResolution resolution = discover(explicitPath);
    if (resolution.valid()) {
        return resolution;
    }

    if (!explicitPath.isEmpty()) {
        if (error) {
            *error = QStringLiteral("The requested DepotDownloader executable is unusable:\n%1").arg(resolution.diagnostic);
        }
        return {};
    }

    if (!installManaged(error)) {
        return {};
    }

    resolution = discover(managedDirectory() + QStringLiteral("/DepotDownloader"));
    if (!resolution.valid() && error) {
        *error = QStringLiteral("The managed DepotDownloader installation did not validate:\n%1").arg(resolution.diagnostic);
    }
    return resolution;
}

ProcessResult DepotDownloaderManager::execute(const QString &executable, const QStringList &arguments, int timeoutMs, bool forwardOutput) const
{
    return m_runner.run(
        executable,
        arguments,
        QFileInfo(executable).absolutePath(),
        QProcessEnvironment::systemEnvironment(),
        timeoutMs,
        &m_account,
        forwardOutput);
}

QString DepotDownloaderManager::managedDirectory() const
{
    return m_dataRoot + QStringLiteral("/depotdownloader/3.4.0");
}

QStringList DepotDownloaderManager::candidatePaths(const QString &explicitPath) const
{
    QStringList paths;
    if (!explicitPath.isEmpty()) {
        paths.push_back(QFileInfo(explicitPath).absoluteFilePath());
    }

    paths.push_back(managedDirectory() + QStringLiteral("/DepotDownloader"));

    const QString upperPath = QStandardPaths::findExecutable(QStringLiteral("DepotDownloader"));
    if (!upperPath.isEmpty()) {
        paths.push_back(upperPath);
    }

    const QString lowerPath = QStandardPaths::findExecutable(QStringLiteral("depotdownloader"));
    if (!lowerPath.isEmpty()) {
        paths.push_back(lowerPath);
    }

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

bool DepotDownloaderManager::validate(const QString &path, QString *version, QString *diagnostic) const
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

    const ProcessResult result = execute(path, {QStringLiteral("--version")}, 120000, false);
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

    const QString output = result.output.trimmed();
    if (!output.contains(QStringLiteral("DepotDownloader"), Qt::CaseInsensitive)) {
        if (diagnostic) {
            *diagnostic = QStringLiteral("unexpected version output: %1").arg(output);
        }
        return false;
    }

    const QRegularExpression versionPattern(QStringLiteral("(?:v)?(\\d+)\\.(\\d+)\\.(\\d+)"));
    const QRegularExpressionMatch versionMatch = versionPattern.match(output);
    if (!versionMatch.hasMatch()) {
        if (diagnostic) {
            *diagnostic = QStringLiteral("unable to determine DepotDownloader version: %1").arg(output);
        }
        return false;
    }

    const int major = versionMatch.captured(1).toInt();
    const int minor = versionMatch.captured(2).toInt();
    const int patch = versionMatch.captured(3).toInt();
    if (major < 3 || (major == 3 && minor < 1)) {
        if (diagnostic) {
            *diagnostic = QStringLiteral("DepotDownloader %1.%2.%3 is too old; version 3.1.0 or newer is required.").arg(major).arg(minor).arg(patch);
        }
        return false;
    }

    if (version) {
        *version = QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
    }
    if (diagnostic) {
        *diagnostic = QStringLiteral("validated successfully");
    }
    return true;
}

bool DepotDownloaderManager::installManaged(QString *error)
{
    const QString architecture = QSysInfo::currentCpuArchitecture().toLower();
    if (architecture != QStringLiteral("x86_64") && architecture != QStringLiteral("amd64")) {
        if (error) {
            *error = QStringLiteral("The managed DepotDownloader build currently supports Linux x86-64 only. Detected architecture: %1").arg(architecture);
        }
        return false;
    }

    if (!AccountResolver::ensureDirectory(m_dataRoot + QStringLiteral("/depotdownloader"), m_account, error)) {
        return false;
    }

    ReleaseAsset asset;
    if (!fetchReleaseAsset(&asset, error)) {
        return false;
    }

    QByteArray archive;
    if (!download(asset.downloadUrl, &archive, error)) {
        return false;
    }

    if (!asset.sha256.isEmpty()) {
        const QByteArray actualDigest = QCryptographicHash::hash(archive, QCryptographicHash::Sha256).toHex();
        if (actualDigest != asset.sha256) {
            if (error) {
                *error = QStringLiteral("DepotDownloader archive checksum mismatch. Expected %1, received %2.").arg(
                    QString::fromLatin1(asset.sha256),
                    QString::fromLatin1(actualDigest));
            }
            return false;
        }
    }

    const QString archivePath = m_dataRoot + QStringLiteral("/depotdownloader/") + releaseAssetName;
    QSaveFile archiveFile(archivePath);
    if (!archiveFile.open(QIODevice::WriteOnly) || archiveFile.write(archive) != archive.size() || !archiveFile.commit()) {
        if (error) {
            *error = QStringLiteral("Unable to save the DepotDownloader archive: %1").arg(archivePath);
        }
        return false;
    }

    if (!AccountResolver::applyOwnership(archivePath, m_account, false, error)) {
        QFile::remove(archivePath);
        return false;
    }

    QString unzip = QStandardPaths::findExecutable(QStringLiteral("unzip"));
    if (unzip.isEmpty()) {
        QFile::remove(archivePath);
        if (error) {
            *error = QStringLiteral("The 'unzip' command is required to install DepotDownloader.");
        }
        return false;
    }

    const QString managedDir = managedDirectory();
    const QString temporaryDir = managedDir + QStringLiteral(".installing");
    if (QFileInfo(temporaryDir).exists() && !QDir(temporaryDir).removeRecursively()) {
        QFile::remove(archivePath);
        if (error) {
            *error = QStringLiteral("Unable to remove the previous temporary DepotDownloader directory: %1").arg(temporaryDir);
        }
        return false;
    }

    if (!AccountResolver::ensureDirectory(temporaryDir, m_account, error)) {
        QFile::remove(archivePath);
        return false;
    }

    const ProcessResult extraction = m_runner.run(
        unzip,
        {QStringLiteral("-q"), QStringLiteral("-o"), archivePath, QStringLiteral("-d"), temporaryDir},
        m_dataRoot,
        QProcessEnvironment::systemEnvironment(),
        300000,
        &m_account,
        true);

    QFile::remove(archivePath);

    if (!extraction.succeeded()) {
        if (error) {
            *error = QStringLiteral("Unable to extract DepotDownloader: %1").arg(
                extraction.errorString.isEmpty() ? extraction.output.trimmed() : extraction.errorString);
        }
        return false;
    }

    const QString temporaryExecutable = temporaryDir + QStringLiteral("/DepotDownloader");
    QFile::setPermissions(
        temporaryExecutable,
        QFile::permissions(temporaryExecutable) | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);

    QString temporaryVersion;
    QString temporaryDiagnostic;
    if (!validate(temporaryExecutable, &temporaryVersion, &temporaryDiagnostic)) {
        if (error) {
            *error = QStringLiteral("The extracted DepotDownloader executable failed validation: %1").arg(temporaryDiagnostic);
        }
        return false;
    }

    if (temporaryVersion != QStringLiteral("3.4.0")) {
        if (error) {
            *error = QStringLiteral("The managed DepotDownloader release returned an unexpected version: %1").arg(temporaryVersion);
        }
        return false;
    }

    if (QFileInfo(managedDir).exists()) {
        const QString backup = managedDir + QStringLiteral(".broken-") + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        if (!QDir().rename(managedDir, backup)) {
            if (error) {
                *error = QStringLiteral("Unable to preserve the existing managed DepotDownloader directory: %1").arg(managedDir);
            }
            return false;
        }
    }

    if (!QDir().rename(temporaryDir, managedDir)) {
        if (error) {
            *error = QStringLiteral("Unable to activate the managed DepotDownloader installation.");
        }
        return false;
    }

    if (!AccountResolver::applyOwnership(managedDir, m_account, true, error)) {
        return false;
    }

    QString installedVersion;
    QString installedDiagnostic;
    if (!validate(managedDir + QStringLiteral("/DepotDownloader"), &installedVersion, &installedDiagnostic)) {
        if (error) {
            *error = QStringLiteral("The managed DepotDownloader installation failed final validation: %1").arg(installedDiagnostic);
        }
        return false;
    }

    return true;
}

bool DepotDownloaderManager::fetchReleaseAsset(ReleaseAsset *asset, QString *error) const
{
    QByteArray metadata;
    QString metadataError;
    if (!download(releaseMetadataUrl, &metadata, &metadataError)) {
        asset->tag = releaseTag;
        asset->downloadUrl = releaseAssetUrl;
        asset->sha256.clear();
        return true;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(metadata, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("Unable to parse DepotDownloader release metadata: %1").arg(parseError.errorString());
        }
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("tag_name")).toString() != releaseTag) {
        if (error) {
            *error = QStringLiteral("DepotDownloader release metadata returned an unexpected tag.");
        }
        return false;
    }

    const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &value : assets) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("name")).toString() != releaseAssetName) {
            continue;
        }

        const QString url = object.value(QStringLiteral("browser_download_url")).toString();
        if (url.isEmpty()) {
            if (error) {
                *error = QStringLiteral("DepotDownloader release metadata is missing the asset URL for %1.").arg(releaseAssetName);
            }
            return false;
        }

        const QString digest = object.value(QStringLiteral("digest")).toString();
        QByteArray sha256;
        if (!digest.isEmpty()) {
            if (!digest.startsWith(QStringLiteral("sha256:"))) {
                if (error) {
                    *error = QStringLiteral("DepotDownloader release metadata contains an unsupported asset digest.");
                }
                return false;
            }

            sha256 = digest.mid(7).toLatin1().toLower();
            if (sha256.size() != 64) {
                if (error) {
                    *error = QStringLiteral("DepotDownloader release metadata contains an invalid SHA-256 digest.");
                }
                return false;
            }
        }

        asset->tag = releaseTag;
        asset->downloadUrl = url;
        asset->sha256 = sha256;
        return true;
    }

    if (error) {
        *error = QStringLiteral("DepotDownloader release asset was not found: %1").arg(releaseAssetName);
    }
    return false;
}

bool DepotDownloaderManager::download(const QString &url, QByteArray *data, QString *error) const
{
    QNetworkAccessManager manager;
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("srcds64-manager/%1").arg(QStringLiteral(SRCDS64_MANAGER_VERSION)));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");

    QNetworkReply *reply = manager.get(request);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer.start(300000);
    loop.exec();

    const bool timedOut = !timer.isActive();
    timer.stop();

    if (reply->error() != QNetworkReply::NoError) {
        if (error) {
            *error = timedOut
                ? QStringLiteral("The download timed out: %1").arg(url)
                : QStringLiteral("Unable to download %1: %2").arg(url, reply->errorString());
        }
        reply->deleteLater();
        return false;
    }

    *data = reply->readAll();
    reply->deleteLater();

    if (data->isEmpty()) {
        if (error) {
            *error = QStringLiteral("The download was empty: %1").arg(url);
        }
        return false;
    }

    return true;
}

}
