#include "core/verifier.h"

#include "core/elf_inspector.h"

#include <QDir>
#include <QFileInfo>

namespace srcds64 {

bool VerificationReport::passed() const
{
    for (const VerificationCheck &check : checks) {
        if (!check.passed) {
            return false;
        }
    }
    return true;
}

VerificationReport Verifier::verify(const GameDefinition &game, const QString &installDirectory, const UnixAccount &account) const
{
    VerificationReport report;
    const QString root = QDir::cleanPath(installDirectory);
    const QString linux64 = root + QStringLiteral("/bin/linux64");

    const auto addFileCheck = [&](const QString &name, const QString &path, bool requireElf64, bool requireExecutable) {
        const QFileInfo info(path);
        bool passed = info.exists() && info.isFile();
        QString detail;

        if (!passed) {
            detail = QStringLiteral("Missing: %1").arg(path);
        } else if (requireElf64 && !ElfInspector::isElf64(path)) {
            passed = false;
            detail = QStringLiteral("Not a 64-bit ELF file: %1").arg(path);
        } else if (requireExecutable && !info.isExecutable()) {
            passed = false;
            detail = QStringLiteral("Not executable: %1").arg(path);
        } else {
            detail = path;
        }

        report.checks.push_back({passed, name, detail});
    };

    report.checks.push_back({QFileInfo(root).isDir(), QStringLiteral("Installation directory"), root});
    addFileCheck(QStringLiteral("64-bit SRCDS launcher"), root + QStringLiteral("/srcds_linux64"), true, true);
    addFileCheck(QStringLiteral("64-bit runner script"), root + QStringLiteral("/srcds_run_64"), false, true);
    addFileCheck(QStringLiteral("64-bit Steam API"), linux64 + QStringLiteral("/libsteam_api.so"), true, false);
    addFileCheck(QStringLiteral("64-bit Steam client"), linux64 + QStringLiteral("/steamclient.so"), true, false);
    addFileCheck(
        QStringLiteral("64-bit game server library"),
        root + QLatin1Char('/') + game.gameDirectory + QStringLiteral("/bin/linux64/server_srv.so"),
        true,
        false);

    QDir libraryDirectory(linux64);
    const QFileInfoList serverLibraries = libraryDirectory.entryInfoList({QStringLiteral("*_srv.so")}, QDir::Files, QDir::Name);
    bool linksPassed = !serverLibraries.isEmpty();
    QStringList linkProblems;

    for (const QFileInfo &library : serverLibraries) {
        QString linkName = library.fileName();
        linkName.replace(QStringLiteral("_srv.so"), QStringLiteral(".so"));
        const QFileInfo link(libraryDirectory.filePath(linkName));

        if (!link.isSymLink() || QFileInfo(link.symLinkTarget()).canonicalFilePath() != library.canonicalFilePath()) {
            linksPassed = false;
            linkProblems.push_back(linkName);
        }
    }

    report.checks.push_back({
        linksPassed,
        QStringLiteral("Engine library links"),
        linksPassed ? QStringLiteral("All *_srv.so aliases are present.") : QStringLiteral("Missing or incorrect links: %1").arg(linkProblems.join(QStringLiteral(", ")))
    });

    const QString sdkSteamClient = account.home + QStringLiteral("/.steam/sdk64/steamclient.so");
    addFileCheck(QStringLiteral("User SDK64 Steam client"), sdkSteamClient, true, false);

    return report;
}

}
