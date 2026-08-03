#pragma once

#include "core/account.h"
#include "core/depot_downloader_manager.h"
#include "core/game_catalog.h"
#include "core/process_runner.h"
#include "core/steamcmd_manager.h"
#include "core/verifier.h"

#include <QList>
#include <QString>

namespace srcds64 {

struct PlanStep {
    QString title;
    QString detail;
};

struct InstallPlan {
    QList<PlanStep> steps;
};

struct InstallOptions {
    GameDefinition game;
    UnixAccount account;
    QString installDirectory;
    QString donorCacheDirectory;
    QString depotDownloaderPath;
    bool validateGameFiles = true;
};

class Installer {
public:
    Installer(ProcessRunner &runner, SteamCmdManager &steamCmdManager, DepotDownloaderManager &depotDownloaderManager);

    InstallPlan plan(
        const InstallOptions &options,
        const SteamCmdResolution &steamCmd,
        const DepotDownloaderResolution &depotDownloader) const;
    bool install(const InstallOptions &options, const SteamCmdResolution &steamCmd, VerificationReport *report, QString *error);

private:
    bool validateFreshDirectory(const QString &path, QString *error) const;
    bool prepareFreshDirectory(const QString &path, const UnixAccount &account, QString *error) const;
    bool ensureDonorCache(const InstallOptions &options, const SteamCmdResolution &steamCmd, QString *error);
    bool populateDonorCache(const QString &sourceRoot, const QString &steamClient, const InstallOptions &options, QString *error) const;
    bool copyFile(const QString &source, const QString &destination, QString *error) const;
    bool createEngineLinks(const QString &linux64Directory, QString *error) const;
    bool writeMarker(const InstallOptions &options, QString *error) const;

    ProcessRunner &m_runner;
    SteamCmdManager &m_steamCmdManager;
    DepotDownloaderManager &m_depotDownloaderManager;
};

}
