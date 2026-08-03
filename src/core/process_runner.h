#pragma once

#include "core/account.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <functional>

namespace srcds64 {

struct ProcessResult {
    bool started = false;
    bool timedOut = false;
    int exitCode = -1;
    QProcess::ExitStatus exitStatus = QProcess::CrashExit;
    QString output;
    QString errorString;

    bool succeeded() const;
};

class ProcessRunner {
public:
    using OutputHandler = std::function<void(const QString &)>;

    explicit ProcessRunner(OutputHandler outputHandler = {});

    ProcessResult run(
        const QString &program,
        const QStringList &arguments,
        const QString &workingDirectory = {},
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment(),
        int timeoutMs = -1,
        const UnixAccount *account = nullptr,
        bool forwardOutput = true) const;

private:
    OutputHandler m_outputHandler;
};

}
