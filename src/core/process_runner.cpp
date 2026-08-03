#include "core/process_runner.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QStandardPaths>

#include <unistd.h>

#include <utility>

namespace srcds64 {

bool ProcessResult::succeeded() const
{
    return started && !timedOut && exitStatus == QProcess::NormalExit && exitCode == 0;
}

ProcessRunner::ProcessRunner(OutputHandler outputHandler)
    : m_outputHandler(std::move(outputHandler))
{
}

ProcessResult ProcessRunner::run(
    const QString &program,
    const QStringList &arguments,
    const QString &workingDirectory,
    QProcessEnvironment environment,
    int timeoutMs,
    const UnixAccount *account,
    bool forwardOutput) const
{
    ProcessResult result;
    QString actualProgram = program;
    QStringList actualArguments = arguments;

    if (account) {
        environment.insert(QStringLiteral("HOME"), account->home);
        environment.insert(QStringLiteral("USER"), account->name);
        environment.insert(QStringLiteral("LOGNAME"), account->name);

        if (geteuid() != account->uid) {
            if (geteuid() != 0) {
                result.errorString = QStringLiteral("Cannot run '%1' as '%2' without root privileges.").arg(program, account->name);
                return result;
            }

            QString runuser = QStandardPaths::findExecutable(QStringLiteral("runuser"));
            if (runuser.isEmpty()) {
                const QStringList candidates = {QStringLiteral("/usr/sbin/runuser"), QStringLiteral("/sbin/runuser")};
                for (const QString &candidate : candidates) {
                    if (QFileInfo(candidate).isExecutable()) {
                        runuser = candidate;
                        break;
                    }
                }
            }

            if (runuser.isEmpty()) {
                result.errorString = QStringLiteral("runuser is required to execute commands as '%1'.").arg(account->name);
                return result;
            }

            QString envProgram = QStandardPaths::findExecutable(QStringLiteral("env"));
            if (envProgram.isEmpty()) {
                envProgram = QStringLiteral("/usr/bin/env");
            }

            QStringList wrappedArguments = {
                QStringLiteral("-u"),
                account->name,
                QStringLiteral("--"),
                envProgram,
                QStringLiteral("HOME=%1").arg(account->home),
                QStringLiteral("USER=%1").arg(account->name),
                QStringLiteral("LOGNAME=%1").arg(account->name),
                program
            };
            wrappedArguments.append(arguments);
            actualProgram = runuser;
            actualArguments = wrappedArguments;
        }
    }

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setProcessEnvironment(environment);
    if (!workingDirectory.isEmpty()) {
        process.setWorkingDirectory(workingDirectory);
    }

    process.start(actualProgram, actualArguments);
    if (!process.waitForStarted(30000)) {
        result.errorString = process.errorString();
        return result;
    }

    result.started = true;
    QElapsedTimer timer;
    timer.start();

    const auto consumeOutput = [&]() {
        const QString chunk = QString::fromLocal8Bit(process.readAll());
        if (chunk.isEmpty()) {
            return;
        }
        result.output += chunk;
        if (forwardOutput && m_outputHandler) {
            m_outputHandler(chunk);
        }
    };

    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(100);
        consumeOutput();

        if (timeoutMs >= 0 && timer.elapsed() >= timeoutMs) {
            result.timedOut = true;
            process.terminate();
            if (!process.waitForFinished(5000)) {
                process.kill();
                process.waitForFinished(5000);
            }
            break;
        }
    }

    consumeOutput();
    result.exitCode = process.exitCode();
    result.exitStatus = process.exitStatus();

    if (result.timedOut) {
        result.errorString = QStringLiteral("The process exceeded its configured timeout.");
    } else if (result.exitStatus == QProcess::CrashExit) {
        result.errorString = process.errorString();
    } else if (process.error() != QProcess::UnknownError && process.error() != QProcess::Timedout) {
        result.errorString = process.errorString();
    }

    return result;
}

}
