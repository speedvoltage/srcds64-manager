#include "core/installer.h"

#include "core/account.h"
#include "core/elf_inspector.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <unistd.h>

namespace srcds64 {

namespace {

constexpr quint32 tf2AppId = 232250;
constexpr quint32 tf2LinuxServerDepotId = 232256;

QString donorFile(const QString &cache, const QString &name)
{
    return QDir::cleanPath(cache) + QLatin1Char('/') + name;
}

bool pathsOverlap(const QString &first, const QString &second)
{
    const QString left = QDir::cleanPath(QFileInfo(first).absoluteFilePath());
    const QString right = QDir::cleanPath(QFileInfo(second).absoluteFilePath());
    return left == right
        || left.startsWith(right + QLatin1Char('/'))
        || right.startsWith(left + QLatin1Char('/'));
}

bool donorCacheValid(const QString &cache)
{
    const QString launcher = donorFile(cache, QStringLiteral("srcds_linux64"));
    const QString runner = donorFile(cache, QStringLiteral("srcds_run_64"));
    const QString steamApi = donorFile(cache, QStringLiteral("libsteam_api.so"));
    const QString steamClient = donorFile(cache, QStringLiteral("steamclient.so"));

    return ElfInspector::isElf64(launcher)
        && QFileInfo(runner).isExecutable()
        && ElfInspector::isElf64(steamApi)
        && ElfInspector::isElf64(steamClient);
}

}

Installer::Installer(ProcessRunner &runner, SteamCmdManager &steamCmdManager, DepotDownloaderManager &depotDownloaderManager)
    : m_runner(runner),
      m_steamCmdManager(steamCmdManager),
      m_depotDownloaderManager(depotDownloaderManager)
{
}

InstallPlan Installer::plan(
    const InstallOptions &options,
    const SteamCmdResolution &steamCmd,
    const DepotDownloaderResolution &depotDownloader) const
{
    const QString depotDownloaderDetail = donorCacheValid(options.donorCacheDirectory)
        ? QStringLiteral("Reuse the verified donor cache without downloading TF2")
        : depotDownloader.valid()
            ? QStringLiteral("Use %1 to download three required SRCDS files from TF2 depot %2; obtain steamclient.so from a verified SteamCMD runtime").arg(depotDownloader.executable).arg(tf2LinuxServerDepotId)
            : QStringLiteral("Install a verified private DepotDownloader copy, download three required SRCDS files from TF2 depot %1, and obtain steamclient.so from a verified SteamCMD runtime").arg(tf2LinuxServerDepotId);

    InstallPlan result;
    result.steps = {
        {QStringLiteral("Account"), QStringLiteral("Run installation commands as %1 with home directory %2").arg(options.account.name, options.account.home)},
        {QStringLiteral("SteamCMD"), steamCmd.valid() ? QStringLiteral("Use %1").arg(steamCmd.executable) : QStringLiteral("Install a private SteamCMD copy under %1").arg(m_steamCmdManager.managedDirectory())},
        {QStringLiteral("Donor downloader"), depotDownloaderDetail},
        {QStringLiteral("Donor cache"), QStringLiteral("Reuse or create the bounded 64-bit runtime cache at %1; a full TF2 installation is never downloaded automatically").arg(options.donorCacheDirectory)},
        {QStringLiteral("Fresh destination"), QStringLiteral("Require an empty destination at %1").arg(options.installDirectory)},
        {QStringLiteral("Game download"), QStringLiteral("Install %1 through anonymous SteamCMD app %2%3").arg(options.game.name).arg(options.game.appId).arg(options.validateGameFiles ? QStringLiteral(" with validation") : QString())},
        {QStringLiteral("64-bit runtime"), QStringLiteral("Install srcds_linux64, srcds_run_64, libsteam_api.so, and steamclient.so")},
        {QStringLiteral("Library aliases"), QStringLiteral("Create non-_srv aliases for every engine library in bin/linux64")},
        {QStringLiteral("Steam SDK64"), QStringLiteral("Preserve an existing valid 64-bit steamclient.so or install a verified copy at %1/.steam/sdk64/steamclient.so").arg(options.account.home)},
        {QStringLiteral("Verification"), QStringLiteral("Verify ELF architecture, executability, game server library, and symlinks")}
    };
    return result;
}

bool Installer::install(const InstallOptions &options, const SteamCmdResolution &steamCmd, VerificationReport *report, QString *error)
{
    if (!steamCmd.valid()) {
        if (error) {
            *error = QStringLiteral("SteamCMD is not available.");
        }
        return false;
    }

    const QString donorPartialDirectory = options.donorCacheDirectory + QStringLiteral(".partial");
    const QString donorReadyDirectory = options.donorCacheDirectory + QStringLiteral(".ready");
    if (pathsOverlap(options.installDirectory, options.donorCacheDirectory)
        || pathsOverlap(options.installDirectory, donorPartialDirectory)
        || pathsOverlap(options.installDirectory, donorReadyDirectory)
        || pathsOverlap(options.installDirectory, m_steamCmdManager.dataRoot())) {
        if (error) {
            *error = QStringLiteral("The installation directory must not overlap the application data, donor cache, or donor work directories.");
        }
        return false;
    }

    if (!validateFreshDirectory(options.installDirectory, error)) {
        return false;
    }

    if (!ensureDonorCache(options, steamCmd, error)) {
        return false;
    }

    if (!prepareFreshDirectory(options.installDirectory, options.account, error)) {
        return false;
    }

    QStringList gameCommands = {
        QStringLiteral("+@sSteamCmdForcePlatformType"),
        QStringLiteral("linux"),
        QStringLiteral("+force_install_dir"),
        options.installDirectory,
        QStringLiteral("+login"),
        QStringLiteral("anonymous"),
        QStringLiteral("+app_info_update"),
        QStringLiteral("1"),
        QStringLiteral("+app_update"),
        QString::number(options.game.appId)
    };
    if (options.validateGameFiles) {
        gameCommands.push_back(QStringLiteral("validate"));
    }
    gameCommands.push_back(QStringLiteral("+quit"));

    ProcessResult gameDownload = m_steamCmdManager.execute(steamCmd.executable, gameCommands, -1, true);
    if (!gameDownload.succeeded() && gameDownload.output.contains(QStringLiteral("Missing configuration"), Qt::CaseInsensitive)) {
        if (steamCmd.managed) {
            QFile::remove(m_steamCmdManager.managedDirectory() + QStringLiteral("/appcache/appinfo.vdf"));
        }
        gameDownload = m_steamCmdManager.execute(steamCmd.executable, gameCommands, -1, true);
    }

    if (!gameDownload.succeeded()) {
        if (error) {
            *error = QStringLiteral("SteamCMD failed to install %1: %2").arg(
                options.game.name,
                gameDownload.errorString.isEmpty() ? gameDownload.output.trimmed() : gameDownload.errorString);
        }
        return false;
    }

    const QString targetLinux64 = QDir::cleanPath(options.installDirectory) + QStringLiteral("/bin/linux64");
    if (!QDir().mkpath(targetLinux64)) {
        if (error) {
            *error = QStringLiteral("Unable to create %1").arg(targetLinux64);
        }
        return false;
    }

    if (!copyFile(donorFile(options.donorCacheDirectory, QStringLiteral("srcds_linux64")), QDir::cleanPath(options.installDirectory) + QStringLiteral("/srcds_linux64"), error)
        || !copyFile(donorFile(options.donorCacheDirectory, QStringLiteral("srcds_run_64")), QDir::cleanPath(options.installDirectory) + QStringLiteral("/srcds_run_64"), error)
        || !copyFile(donorFile(options.donorCacheDirectory, QStringLiteral("libsteam_api.so")), targetLinux64 + QStringLiteral("/libsteam_api.so"), error)
        || !copyFile(donorFile(options.donorCacheDirectory, QStringLiteral("steamclient.so")), targetLinux64 + QStringLiteral("/steamclient.so"), error)) {
        return false;
    }

    QFile::setPermissions(
        QDir::cleanPath(options.installDirectory) + QStringLiteral("/srcds_linux64"),
        QFile::permissions(QDir::cleanPath(options.installDirectory) + QStringLiteral("/srcds_linux64")) | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
    QFile::setPermissions(
        QDir::cleanPath(options.installDirectory) + QStringLiteral("/srcds_run_64"),
        QFile::permissions(QDir::cleanPath(options.installDirectory) + QStringLiteral("/srcds_run_64")) | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);

    if (!createEngineLinks(targetLinux64, error)) {
        return false;
    }

    const QString sdk64 = options.account.home + QStringLiteral("/.steam/sdk64");
    const QString sdkSteamClient = sdk64 + QStringLiteral("/steamclient.so");
    bool installedSdkSteamClient = false;

    if (!AccountResolver::ensureDirectory(sdk64, options.account, error)) {
        return false;
    }

    if (!ElfInspector::isElf64(sdkSteamClient)) {
        if (QFileInfo::exists(sdkSteamClient) || QFileInfo(sdkSteamClient).isSymLink()) {
            const QString backup = sdkSteamClient + QStringLiteral(".srcds64-backup-") + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"));
            if (!QFile::rename(sdkSteamClient, backup)) {
                if (error) {
                    *error = QStringLiteral("Unable to preserve the existing SDK64 steamclient.so.");
                }
                return false;
            }
        }

        if (!copyFile(donorFile(options.donorCacheDirectory, QStringLiteral("steamclient.so")), sdkSteamClient, error)) {
            return false;
        }
        installedSdkSteamClient = true;
    }

    if (!writeMarker(options, error)) {
        return false;
    }

    if (!AccountResolver::applyOwnership(options.installDirectory, options.account, true, error)
        || !AccountResolver::applyOwnership(sdk64, options.account, false, error)
        || (installedSdkSteamClient && !AccountResolver::applyOwnership(sdkSteamClient, options.account, false, error))) {
        return false;
    }

    Verifier verifier;
    const VerificationReport verification = verifier.verify(options.game, options.installDirectory, options.account);
    if (report) {
        *report = verification;
    }

    if (!verification.passed()) {
        if (error) {
            *error = QStringLiteral("Installation completed, but verification failed.");
        }
        return false;
    }

    return true;
}

bool Installer::validateFreshDirectory(const QString &path, QString *error) const
{
    const QFileInfo info(path);
    if (!info.exists()) {
        return true;
    }

    if (!info.isDir()) {
        if (error) {
            *error = QStringLiteral("The installation destination is not a directory: %1").arg(path);
        }
        return false;
    }

    const QDir directory(path);
    if (!directory.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).isEmpty()) {
        if (error) {
            *error = QStringLiteral("Fresh installations require an empty destination: %1").arg(path);
        }
        return false;
    }

    return true;
}

bool Installer::prepareFreshDirectory(const QString &path, const UnixAccount &account, QString *error) const
{
    return validateFreshDirectory(path, error) && AccountResolver::ensureDirectory(path, account, error);
}

bool Installer::ensureDonorCache(const InstallOptions &options, const SteamCmdResolution &steamCmd, QString *error)
{
    if (donorCacheValid(options.donorCacheDirectory)) {
        return true;
    }

    QString steamClientError;
    const QString steamClient = m_steamCmdManager.ensureSteamClient64(steamCmd, &steamClientError);
    if (steamClient.isEmpty()) {
        if (error) {
            *error = steamClientError;
        }
        return false;
    }

    QString downloaderError;
    const DepotDownloaderResolution downloader = m_depotDownloaderManager.ensure(options.depotDownloaderPath, &downloaderError);
    if (!downloader.valid()) {
        if (error) {
            *error = downloaderError;
        }
        return false;
    }

    const QString partialDirectory = options.donorCacheDirectory + QStringLiteral(".partial");
    if (!AccountResolver::ensureDirectory(partialDirectory, options.account, error)) {
        return false;
    }

    const QString fileListPath = partialDirectory + QStringLiteral("/srcds64-runtime-files.txt");
    const QByteArray fileList = QByteArrayLiteral(
        "srcds_linux64\n"
        "srcds_run_64\n"
        "bin/linux64/libsteam_api.so\n");

    QSaveFile fileListFile(fileListPath);
    if (!fileListFile.open(QIODevice::WriteOnly)
        || fileListFile.write(fileList) != fileList.size()
        || !fileListFile.commit()) {
        if (error) {
            *error = QStringLiteral("Unable to write the TF2 donor file list: %1").arg(fileListPath);
        }
        return false;
    }

    if (!AccountResolver::applyOwnership(fileListPath, options.account, false, error)) {
        return false;
    }

    const QStringList arguments = {
        QStringLiteral("-app"),
        QString::number(tf2AppId),
        QStringLiteral("-depot"),
        QString::number(tf2LinuxServerDepotId),
        QStringLiteral("-os"),
        QStringLiteral("linux"),
        QStringLiteral("-osarch"),
        QStringLiteral("64"),
        QStringLiteral("-dir"),
        partialDirectory,
        QStringLiteral("-filelist"),
        fileListPath,
        QStringLiteral("-validate")
    };

    const ProcessResult download = m_depotDownloaderManager.execute(downloader.executable, arguments, -1, true);
    if (!download.succeeded()) {
        if (error) {
            const QString detail = download.errorString.isEmpty() ? download.output.trimmed() : download.errorString;
            *error = QStringLiteral(
                "DepotDownloader failed to acquire the bounded TF2 donor runtime: %1\n"
                "No full TF2 fallback was attempted. Partial files were preserved at %2 for a later retry.")
                         .arg(detail, partialDirectory);
        }
        return false;
    }

    if (!populateDonorCache(partialDirectory, steamClient, options, error)) {
        return false;
    }

    QDir(partialDirectory).removeRecursively();
    return true;
}

bool Installer::populateDonorCache(
    const QString &sourceRoot,
    const QString &steamClient,
    const InstallOptions &options,
    QString *error) const
{
    const QString launcher = QDir::cleanPath(sourceRoot) + QStringLiteral("/srcds_linux64");
    const QString runner = QDir::cleanPath(sourceRoot) + QStringLiteral("/srcds_run_64");
    const QString steamApi = QDir::cleanPath(sourceRoot) + QStringLiteral("/bin/linux64/libsteam_api.so");

    QStringList validationProblems;
    if (!ElfInspector::isElf64(launcher)) {
        validationProblems.push_back(QStringLiteral("srcds_linux64 is missing or is not ELF64: %1").arg(launcher));
    }
    if (!QFileInfo(runner).isFile()) {
        validationProblems.push_back(QStringLiteral("srcds_run_64 is missing: %1").arg(runner));
    }
    if (!ElfInspector::isElf64(steamApi)) {
        validationProblems.push_back(QStringLiteral("libsteam_api.so is missing or is not ELF64: %1").arg(steamApi));
    }
    if (!ElfInspector::isElf64(steamClient)) {
        validationProblems.push_back(QStringLiteral("SteamCMD did not provide a verified ELF64 steamclient.so: %1").arg(steamClient));
    }

    if (!validationProblems.isEmpty()) {
        if (error) {
            *error = QStringLiteral("The bounded donor runtime failed validation:\n  - %1").arg(
                validationProblems.join(QStringLiteral("\n  - ")));
        }
        return false;
    }

    const QString readyDirectory = options.donorCacheDirectory + QStringLiteral(".ready");
    const QFileInfo readyInfo(readyDirectory);
    if (readyInfo.exists() && !QDir(readyDirectory).removeRecursively()) {
        if (error) {
            *error = QStringLiteral("Unable to remove the previous donor activation directory: %1").arg(readyDirectory);
        }
        return false;
    }

    if (!AccountResolver::ensureDirectory(readyDirectory, options.account, error)) {
        return false;
    }

    if (!copyFile(launcher, donorFile(readyDirectory, QStringLiteral("srcds_linux64")), error)
        || !copyFile(runner, donorFile(readyDirectory, QStringLiteral("srcds_run_64")), error)
        || !copyFile(steamApi, donorFile(readyDirectory, QStringLiteral("libsteam_api.so")), error)
        || !copyFile(steamClient, donorFile(readyDirectory, QStringLiteral("steamclient.so")), error)) {
        return false;
    }

    QFile::setPermissions(
        donorFile(readyDirectory, QStringLiteral("srcds_linux64")),
        QFile::permissions(donorFile(readyDirectory, QStringLiteral("srcds_linux64"))) | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
    QFile::setPermissions(
        donorFile(readyDirectory, QStringLiteral("srcds_run_64")),
        QFile::permissions(donorFile(readyDirectory, QStringLiteral("srcds_run_64"))) | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);

    if (!donorCacheValid(readyDirectory)) {
        if (error) {
            *error = QStringLiteral("The donor cache failed validation before activation: %1").arg(readyDirectory);
        }
        return false;
    }

    const QFileInfo finalInfo(options.donorCacheDirectory);
    if (finalInfo.exists()) {
        if (!finalInfo.isDir()) {
            if (error) {
                *error = QStringLiteral("The donor cache path exists but is not a directory: %1").arg(options.donorCacheDirectory);
            }
            return false;
        }

        QDir finalDirectory(options.donorCacheDirectory);
        if (finalDirectory.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).isEmpty()) {
            if (!QDir().rmdir(options.donorCacheDirectory)) {
                if (error) {
                    *error = QStringLiteral("Unable to remove the empty donor cache directory before activation: %1").arg(options.donorCacheDirectory);
                }
                return false;
            }
        } else {
            const QString backup = options.donorCacheDirectory
                + QStringLiteral(".invalid-")
                + QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
            if (!QDir().rename(options.donorCacheDirectory, backup)) {
                if (error) {
                    *error = QStringLiteral("Unable to preserve the invalid donor cache at %1").arg(backup);
                }
                return false;
            }
        }
    }

    if (!QDir().rename(readyDirectory, options.donorCacheDirectory)) {
        if (error) {
            *error = QStringLiteral("Unable to activate the verified donor cache: %1").arg(options.donorCacheDirectory);
        }
        return false;
    }

    if (!AccountResolver::applyOwnership(options.donorCacheDirectory, options.account, true, error)) {
        return false;
    }

    if (!donorCacheValid(options.donorCacheDirectory)) {
        if (error) {
            *error = QStringLiteral("The activated donor cache failed final validation: %1").arg(options.donorCacheDirectory);
        }
        return false;
    }

    return true;
}

bool Installer::copyFile(const QString &source, const QString &destination, QString *error) const
{
    const QFileInfo sourceInfo(source);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        if (error) {
            *error = QStringLiteral("Source file is missing: %1").arg(source);
        }
        return false;
    }

    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
        if (error) {
            *error = QStringLiteral("Unable to create destination directory for %1").arg(destination);
        }
        return false;
    }

    if ((QFileInfo(destination).exists() || QFileInfo(destination).isSymLink()) && !QFile::remove(destination)) {
        if (error) {
            *error = QStringLiteral("Unable to replace %1").arg(destination);
        }
        return false;
    }

    if (!QFile::copy(source, destination)) {
        if (error) {
            *error = QStringLiteral("Unable to copy %1 to %2").arg(source, destination);
        }
        return false;
    }

    if (!QFile::setPermissions(destination, sourceInfo.permissions())) {
        if (error) {
            *error = QStringLiteral("Unable to preserve permissions on %1").arg(destination);
        }
        return false;
    }

    return true;
}

bool Installer::createEngineLinks(const QString &linux64Directory, QString *error) const
{
    QDir directory(linux64Directory);
    const QFileInfoList libraries = directory.entryInfoList({QStringLiteral("*_srv.so")}, QDir::Files, QDir::Name);
    if (libraries.isEmpty()) {
        if (error) {
            *error = QStringLiteral("No *_srv.so libraries were found in %1").arg(linux64Directory);
        }
        return false;
    }

    for (const QFileInfo &library : libraries) {
        QString linkName = library.fileName();
        linkName.replace(QStringLiteral("_srv.so"), QStringLiteral(".so"));
        const QString linkPath = directory.filePath(linkName);

        if (QFileInfo::exists(linkPath) || QFileInfo(linkPath).isSymLink()) {
            const QFileInfo existing(linkPath);
            if (existing.isSymLink() && QFileInfo(existing.symLinkTarget()).canonicalFilePath() == library.canonicalFilePath()) {
                continue;
            }
            if (error) {
                *error = QStringLiteral("Refusing to replace existing library alias: %1").arg(linkPath);
            }
            return false;
        }

        const QByteArray target = QFile::encodeName(library.fileName());
        const QByteArray encodedLink = QFile::encodeName(linkPath);
        if (symlink(target.constData(), encodedLink.constData()) != 0) {
            if (error) {
                *error = QStringLiteral("Unable to create library alias: %1").arg(linkPath);
            }
            return false;
        }
    }

    return true;
}

bool Installer::writeMarker(const InstallOptions &options, QString *error) const
{
    QJsonObject object;
    object.insert(QStringLiteral("schemaVersion"), 1);
    object.insert(QStringLiteral("managerVersion"), QStringLiteral(SRCDS64_MANAGER_VERSION));
    object.insert(QStringLiteral("game"), options.game.id);
    object.insert(QStringLiteral("appId"), static_cast<qint64>(options.game.appId));
    object.insert(QStringLiteral("installedAtUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    object.insert(QStringLiteral("owner"), options.account.name);
    object.insert(QStringLiteral("donorAppId"), static_cast<qint64>(tf2AppId));
    object.insert(QStringLiteral("donorDepotId"), static_cast<qint64>(tf2LinuxServerDepotId));
    object.insert(QStringLiteral("donorMethod"), QStringLiteral("DepotDownloader file list plus SteamCMD ELF64 steamclient"));

    QSaveFile file(QDir::cleanPath(options.installDirectory) + QStringLiteral("/.srcds64-manager.json"));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        if (error) {
            *error = QStringLiteral("Unable to write the installation marker.");
        }
        return false;
    }

    return true;
}

}
