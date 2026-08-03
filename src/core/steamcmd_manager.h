#pragma once

#include "core/account.h"
#include "core/process_runner.h"

#include <QString>
#include <QStringList>

namespace srcds64 {

struct SteamCmdResolution {
    QString executable;
    bool managed = false;
    QString diagnostic;

    bool valid() const;
};

class SteamCmdManager {
public:
    SteamCmdManager(ProcessRunner &runner, UnixAccount account, QString dataRoot);

    SteamCmdResolution discover(const QString &explicitPath = {}) const;
    SteamCmdResolution ensure(const QString &explicitPath = {}, QString *error = nullptr);
    ProcessResult execute(const QString &executable, const QStringList &commands, int timeoutMs = -1, bool forwardOutput = true) const;
    QString ensureSteamClient64(const SteamCmdResolution &resolution, QString *error = nullptr);

    QString dataRoot() const;
    QString managedDirectory() const;

private:
    QStringList candidatePaths(const QString &explicitPath) const;
    QString findSteamClient64(const QString &executable) const;
    bool validate(const QString &path, QString *diagnostic = nullptr) const;
    bool installManaged(QString *error);
    bool downloadArchive(const QString &destination, QString *error) const;

    ProcessRunner &m_runner;
    UnixAccount m_account;
    QString m_dataRoot;
};

}
