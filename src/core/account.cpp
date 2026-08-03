#include "core/account.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <sys/stat.h>

#include <cerrno>
#include <cstring>
#include <pwd.h>
#include <unistd.h>

namespace srcds64 {

QString AccountResolver::defaultAccountName()
{
    if (geteuid() == 0) {
        const QByteArray sudoUser = qgetenv("SUDO_USER");
        if (!sudoUser.isEmpty() && sudoUser != "root") {
            return QString::fromLocal8Bit(sudoUser);
        }
    }

    const passwd *entry = getpwuid(getuid());
    if (entry && entry->pw_name) {
        return QString::fromLocal8Bit(entry->pw_name);
    }

    return QString::fromLocal8Bit(qgetenv("USER"));
}

std::optional<UnixAccount> AccountResolver::resolve(const QString &name, QString *error)
{
    const QString resolvedName = name.isEmpty() || name == QStringLiteral("current") ? defaultAccountName() : name;
    const QByteArray encodedName = QFile::encodeName(resolvedName);
    const passwd *entry = getpwnam(encodedName.constData());

    if (!entry || !entry->pw_dir) {
        if (error) {
            *error = QStringLiteral("Linux account '%1' does not exist.").arg(resolvedName);
        }
        return std::nullopt;
    }

    UnixAccount account;
    account.name = QString::fromLocal8Bit(entry->pw_name);
    account.home = QString::fromLocal8Bit(entry->pw_dir);
    account.uid = entry->pw_uid;
    account.gid = entry->pw_gid;
    return account;
}

bool AccountResolver::canRunAs(const UnixAccount &account, QString *error)
{
    if (geteuid() == account.uid || geteuid() == 0) {
        return true;
    }

    if (error) {
        *error = QStringLiteral("The application cannot run commands as '%1'. Run it as that user or as root.").arg(account.name);
    }
    return false;
}

bool AccountResolver::ensureDirectory(const QString &path, const UnixAccount &account, QString *error)
{
    const QString cleanPath = QDir::cleanPath(path);
    const QFileInfo existing(cleanPath);

    if (existing.exists()) {
        if (!existing.isDir()) {
            if (error) {
                *error = QStringLiteral("Path exists but is not a directory: %1").arg(cleanPath);
            }
            return false;
        }

        if (geteuid() == 0 && account.uid != 0) {
            struct stat status {};
            const QByteArray encodedPath = QFile::encodeName(cleanPath);
            if (stat(encodedPath.constData(), &status) != 0) {
                if (error) {
                    *error = QStringLiteral("Unable to inspect directory ownership: %1").arg(cleanPath);
                }
                return false;
            }

            const bool writable = (status.st_uid == account.uid && (status.st_mode & S_IWUSR) != 0)
                || (status.st_gid == account.gid && (status.st_mode & S_IWGRP) != 0)
                || (status.st_mode & S_IWOTH) != 0;
            if (!writable) {
                if (error) {
                    *error = QStringLiteral("Directory is not writable by '%1': %2").arg(account.name, cleanPath);
                }
                return false;
            }
        } else if (!existing.isWritable()) {
            if (error) {
                *error = QStringLiteral("Directory is not writable: %1").arg(cleanPath);
            }
            return false;
        }

        return true;
    }

    QStringList createdPaths;
    QString cursor = cleanPath;
    while (!QFileInfo::exists(cursor)) {
        createdPaths.prepend(cursor);
        const QString parent = QFileInfo(cursor).absolutePath();
        if (parent == cursor) {
            break;
        }
        cursor = parent;
    }

    if (!QDir().mkpath(cleanPath)) {
        if (error) {
            *error = QStringLiteral("Unable to create directory: %1").arg(cleanPath);
        }
        return false;
    }

    for (const QString &createdPath : createdPaths) {
        if (!applyOwnership(createdPath, account, false, error)) {
            return false;
        }
    }

    return true;
}

bool AccountResolver::applyOwnership(const QString &path, const UnixAccount &account, bool recursive, QString *error)
{
    if (geteuid() != 0 || account.uid == 0) {
        return true;
    }

    const auto apply = [&](const QString &entryPath) {
        const QByteArray encodedPath = QFile::encodeName(entryPath);
        if (lchown(encodedPath.constData(), account.uid, account.gid) != 0) {
            if (error) {
                *error = QStringLiteral("Unable to change ownership of %1: %2").arg(entryPath, QString::fromLocal8Bit(std::strerror(errno)));
            }
            return false;
        }
        return true;
    };

    if (!apply(path)) {
        return false;
    }

    if (!recursive) {
        return true;
    }

    QDirIterator iterator(path, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        if (!apply(iterator.next())) {
            return false;
        }
    }

    return true;
}

}
