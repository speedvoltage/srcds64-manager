#include "core/account.h"
#include "core/depot_downloader_manager.h"
#include "core/game_catalog.h"
#include "core/installer.h"
#include "core/process_runner.h"
#include "core/steamcmd_manager.h"
#include "core/verifier.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>

#include <cstdio>
#include <optional>
#include <unistd.h>

namespace {

bool interactiveInput()
{
    return isatty(fileno(stdin)) != 0;
}

QString absolutePath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString shellQuote(const QString &value)
{
    QString quoted = value;
    quoted.replace(QLatin1Char('\''), QStringLiteral("'\"'\"'"));
    return QStringLiteral("'") + quoted + QStringLiteral("'");
}

QString prompt(QTextStream &input, QTextStream &output, const QString &label, const QString &defaultValue)
{
    output << label;
    if (!defaultValue.isEmpty()) {
        output << " [" << defaultValue << ']';
    }
    output << ": " << Qt::flush;
    const QString value = input.readLine().trimmed();
    return value.isEmpty() ? defaultValue : value;
}

bool confirm(QTextStream &input, QTextStream &output, const QString &label, bool defaultYes)
{
    output << label << (defaultYes ? " [Y/n]: " : " [y/N]: ") << Qt::flush;
    const QString value = input.readLine().trimmed().toLower();
    if (value.isEmpty()) {
        return defaultYes;
    }
    return value == QStringLiteral("y") || value == QStringLiteral("yes");
}

QString selectOwner(QTextStream &input, QTextStream &output, const QString &requested)
{
    if (!requested.isEmpty()) {
        return requested;
    }

    const QString defaultOwner = srcds64::AccountResolver::defaultAccountName();
    if (!interactiveInput()) {
        return defaultOwner;
    }

    if (confirm(input, output, QStringLiteral("Use the current account '%1'?").arg(defaultOwner), true)) {
        return defaultOwner;
    }

    return prompt(input, output, QStringLiteral("Existing Linux account"), defaultOwner);
}

void printPlan(QTextStream &output, const srcds64::InstallPlan &plan)
{
    output << '\n';
    int index = 1;
    for (const srcds64::PlanStep &step : plan.steps) {
        output << index++ << ". " << step.title << '\n';
        output << "   " << step.detail << '\n';
    }
    output << '\n';
}

void printVerification(QTextStream &output, const srcds64::VerificationReport &report)
{
    for (const srcds64::VerificationCheck &check : report.checks) {
        output << (check.passed ? "[PASS] " : "[FAIL] ") << check.name << '\n';
        output << "       " << check.detail << '\n';
    }
}

void printInstallationSuccess(
    QTextStream &output,
    const srcds64::GameDefinition &game,
    const QString &installDirectory)
{
    const QString quotedDirectory = shellQuote(installDirectory);
    const QString launchArguments = QStringLiteral("-game %1 +map %2")
                                        .arg(game.gameDirectory, game.defaultMap);

    output << "\n============================================================\n";
    output << game.name << " 64-bit server installed successfully.\n";
    output << "============================================================\n\n";
    output << "Server directory:\n";
    output << "  " << installDirectory << "\n\n";
    output << "Start the server:\n";
    output << "  cd " << quotedDirectory << '\n';
    output << "  ./srcds_run_64 " << launchArguments << "\n\n";
    output << "If the launcher reports missing shared libraries:\n";
    output << "  cd " << quotedDirectory << '\n';
    output << "  export LD_LIBRARY_PATH=\"$PWD/bin:$PWD/bin/linux64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}\"\n";
    output << "  ./srcds_run_64 " << launchArguments << '\n';
}

std::optional<srcds64::GameDefinition> requireGame(const QString &id, QTextStream &errorOutput)
{
    if (id.isEmpty()) {
        errorOutput << "A game is required. Use --game css, dods, hl2dm, or hldm.\n";
        return std::nullopt;
    }

    QString error;
    const auto game = srcds64::GameCatalog::find(id, &error);
    if (!game) {
        errorOutput << error << '\n';
    }
    return game;
}

}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("srcds64"));
    QCoreApplication::setApplicationVersion(QStringLiteral(SRCDS64_MANAGER_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("srcds64-manager"));

    QTextStream input(stdin);
    QTextStream output(stdout);
    QTextStream errorOutput(stderr);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Fresh 64-bit Source Dedicated Server installer for Linux"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("games, doctor, plan, install, or verify"));

    const QCommandLineOption gameOption({QStringLiteral("g"), QStringLiteral("game")}, QStringLiteral("Game identifier: css, dods, hl2dm, or hldm"), QStringLiteral("game"));
    const QCommandLineOption installDirectoryOption({QStringLiteral("d"), QStringLiteral("install-dir")}, QStringLiteral("Fresh server installation directory"), QStringLiteral("path"));
    const QCommandLineOption donorCacheOption(QStringLiteral("donor-cache"), QStringLiteral("TF2 Linux server donor cache directory"), QStringLiteral("path"));
    const QCommandLineOption steamCmdOption(QStringLiteral("steamcmd"), QStringLiteral("Explicit SteamCMD executable"), QStringLiteral("path"));
    const QCommandLineOption depotDownloaderOption(QStringLiteral("depot-downloader"), QStringLiteral("Explicit DepotDownloader executable"), QStringLiteral("path"));
    const QCommandLineOption ownerOption(QStringLiteral("owner"), QStringLiteral("Current or existing Linux account used for the installation"), QStringLiteral("account"));
    const QCommandLineOption dataDirectoryOption(QStringLiteral("data-dir"), QStringLiteral("Application data and managed tool directory"), QStringLiteral("path"));
    const QCommandLineOption yesOption({QStringLiteral("y"), QStringLiteral("yes")}, QStringLiteral("Accept the installation plan without prompting"));
    const QCommandLineOption noValidateOption(QStringLiteral("no-validate"), QStringLiteral("Do not request SteamCMD file validation"));

    parser.addOptions({
        gameOption,
        installDirectoryOption,
        donorCacheOption,
        steamCmdOption,
        depotDownloaderOption,
        ownerOption,
        dataDirectoryOption,
        yesOption,
        noValidateOption
    });
    parser.process(application);

    const QStringList positionalArguments = parser.positionalArguments();
    if (positionalArguments.isEmpty()) {
        parser.showHelp(1);
    }

    const QString command = positionalArguments.first().toLower();
    const QSet<QString> supportedCommands = {
        QStringLiteral("games"),
        QStringLiteral("doctor"),
        QStringLiteral("plan"),
        QStringLiteral("install"),
        QStringLiteral("verify")
    };

    if (!supportedCommands.contains(command)) {
        errorOutput << "Unknown command: " << command << '\n';
        return 1;
    }

    if (command == QStringLiteral("games")) {
        QString error;
        const QList<srcds64::GameDefinition> games = srcds64::GameCatalog::all(&error);
        if (!error.isEmpty()) {
            errorOutput << error << '\n';
            return 1;
        }

        for (const srcds64::GameDefinition &game : games) {
            output << game.id << "\t" << game.appId << "\t" << game.name << "\t" << game.gameDirectory << "\t" << game.defaultMap << '\n';
        }
        return 0;
    }

    const QString ownerName = selectOwner(input, output, parser.value(ownerOption));
    QString accountError;
    const auto account = srcds64::AccountResolver::resolve(ownerName, &accountError);
    if (!account || !srcds64::AccountResolver::canRunAs(*account, &accountError)) {
        errorOutput << accountError << '\n';
        return 1;
    }

    const QString defaultDataDirectory = account->home + QStringLiteral("/.local/share/srcds64-manager");
    const QString dataDirectory = absolutePath(parser.value(dataDirectoryOption).isEmpty() ? defaultDataDirectory : parser.value(dataDirectoryOption));

    srcds64::ProcessRunner runner([&output](const QString &chunk) {
        output << chunk << Qt::flush;
    });
    srcds64::SteamCmdManager steamCmdManager(runner, *account, dataDirectory);
    srcds64::DepotDownloaderManager depotDownloaderManager(runner, *account, dataDirectory);

    if (command == QStringLiteral("doctor")) {
        QString error;
        const srcds64::SteamCmdResolution steamCmd = steamCmdManager.ensure(parser.value(steamCmdOption), &error);
        if (!steamCmd.valid()) {
            errorOutput << error << '\n';
            return 1;
        }

        const QString steamClient64 = steamCmdManager.ensureSteamClient64(steamCmd, &error);
        if (steamClient64.isEmpty()) {
            errorOutput << error << '\n';
            return 1;
        }

        const srcds64::DepotDownloaderResolution depotDownloader = depotDownloaderManager.ensure(parser.value(depotDownloaderOption), &error);
        if (!depotDownloader.valid()) {
            errorOutput << error << '\n';
            return 1;
        }

        output << "SteamCMD: " << steamCmd.executable << '\n';
        output << "SteamCMD managed: " << (steamCmd.managed ? "yes" : "no") << '\n';
        output << "SteamCMD status: " << steamCmd.diagnostic << '\n';
        output << "SteamCMD 64-bit client: " << steamClient64 << '\n';
        output << "SteamCMD 64-bit client status: validated successfully\n";
        output << "DepotDownloader: " << depotDownloader.executable << '\n';
        output << "DepotDownloader managed: " << (depotDownloader.managed ? "yes" : "no") << '\n';
        output << "DepotDownloader version: " << depotDownloader.version << '\n';
        output << "DepotDownloader status: " << depotDownloader.diagnostic << '\n';

        const QString legacyTf2Directory = dataDirectory + QStringLiteral("/donor/tf2-full");
        if (QFileInfo::exists(legacyTf2Directory)) {
            output << "Legacy full TF2 cache: " << legacyTf2Directory << '\n';
            output << "Legacy cache status: unused; remove it manually after confirming no other process needs it.\n";
        }
        return 0;
    }

    const auto game = requireGame(parser.value(gameOption), errorOutput);
    if (!game) {
        return 1;
    }

    QString installDirectory = parser.value(installDirectoryOption);
    const QString defaultInstallDirectory = account->home + QStringLiteral("/servers/") + game->id + QStringLiteral("-server");
    if (installDirectory.isEmpty()) {
        installDirectory = interactiveInput()
            ? prompt(input, output, QStringLiteral("Installation directory"), defaultInstallDirectory)
            : defaultInstallDirectory;
    }
    installDirectory = absolutePath(installDirectory);

    const QString donorCacheDirectory = absolutePath(
        parser.value(donorCacheOption).isEmpty()
            ? dataDirectory + QStringLiteral("/donor/tf2-linux64")
            : parser.value(donorCacheOption));

    srcds64::InstallOptions installOptions;
    installOptions.game = *game;
    installOptions.account = *account;
    installOptions.installDirectory = installDirectory;
    installOptions.donorCacheDirectory = donorCacheDirectory;
    installOptions.depotDownloaderPath = parser.value(depotDownloaderOption);
    installOptions.validateGameFiles = !parser.isSet(noValidateOption);

    srcds64::Installer installer(runner, steamCmdManager, depotDownloaderManager);

    if (command == QStringLiteral("plan")) {
        const srcds64::SteamCmdResolution steamCmd = steamCmdManager.discover(parser.value(steamCmdOption));
        const srcds64::DepotDownloaderResolution depotDownloader = depotDownloaderManager.discover(parser.value(depotDownloaderOption));
        printPlan(output, installer.plan(installOptions, steamCmd, depotDownloader));
        if (!steamCmd.valid() && !steamCmd.diagnostic.isEmpty()) {
            output << "SteamCMD diagnostics:\n" << steamCmd.diagnostic << '\n';
        }
        if (!depotDownloader.valid() && !depotDownloader.diagnostic.isEmpty()) {
            output << "DepotDownloader diagnostics:\n" << depotDownloader.diagnostic << '\n';
        }
        return 0;
    }

    if (command == QStringLiteral("install")) {
        const srcds64::SteamCmdResolution discoveredSteamCmd = steamCmdManager.discover(parser.value(steamCmdOption));
        const srcds64::DepotDownloaderResolution discoveredDepotDownloader = depotDownloaderManager.discover(parser.value(depotDownloaderOption));
        printPlan(output, installer.plan(installOptions, discoveredSteamCmd, discoveredDepotDownloader));

        if (!parser.isSet(yesOption)) {
            if (!interactiveInput()) {
                errorOutput << "Non-interactive installation requires --yes.\n";
                return 1;
            }
            if (!confirm(input, output, QStringLiteral("Proceed with this fresh installation?"), false)) {
                output << "Installation cancelled.\n";
                return 0;
            }
        }

        QString error;
        const srcds64::SteamCmdResolution steamCmd = steamCmdManager.ensure(parser.value(steamCmdOption), &error);
        if (!steamCmd.valid()) {
            errorOutput << error << '\n';
            return 1;
        }

        srcds64::VerificationReport report;
        if (!installer.install(installOptions, steamCmd, &report, &error)) {
            if (!report.checks.isEmpty()) {
                printVerification(output, report);
            }
            errorOutput << error << '\n';
            return 1;
        }

        printVerification(output, report);
        printInstallationSuccess(output, *game, installDirectory);
        return 0;
    }

    srcds64::Verifier verifier;
    const srcds64::VerificationReport report = verifier.verify(*game, installDirectory, *account);
    printVerification(output, report);
    return report.passed() ? 0 : 1;
}
