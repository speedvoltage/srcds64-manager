#pragma once

#include "core/account.h"
#include "core/process_runner.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace srcds64 {

struct DepotDownloaderResolution {
    QString executable;
    QString version;
    bool managed = false;
    QString diagnostic;

    bool valid() const;
};

class DepotDownloaderManager {
public:
    DepotDownloaderManager(ProcessRunner &runner, UnixAccount account, QString dataRoot);

    DepotDownloaderResolution discover(const QString &explicitPath = {}) const;
    DepotDownloaderResolution ensure(const QString &explicitPath = {}, QString *error = nullptr);
    ProcessResult execute(const QString &executable, const QStringList &arguments, int timeoutMs = -1, bool forwardOutput = true) const;

    QString managedDirectory() const;

private:
    struct ReleaseAsset {
        QString tag;
        QString downloadUrl;
        QByteArray sha256;
    };

    QStringList candidatePaths(const QString &explicitPath) const;
    bool validate(const QString &path, QString *version = nullptr, QString *diagnostic = nullptr) const;
    bool installManaged(QString *error);
    bool fetchReleaseAsset(ReleaseAsset *asset, QString *error) const;
    bool download(const QString &url, QByteArray *data, QString *error) const;

    ProcessRunner &m_runner;
    UnixAccount m_account;
    QString m_dataRoot;
};

}
