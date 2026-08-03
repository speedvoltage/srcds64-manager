#pragma once

#include <QString>

#include <optional>
#include <sys/types.h>

namespace srcds64 {

struct UnixAccount {
    QString name;
    QString home;
    uid_t uid;
    gid_t gid;
};

class AccountResolver {
public:
    static QString defaultAccountName();
    static std::optional<UnixAccount> resolve(const QString &name, QString *error = nullptr);
    static bool canRunAs(const UnixAccount &account, QString *error = nullptr);
    static bool ensureDirectory(const QString &path, const UnixAccount &account, QString *error = nullptr);
    static bool applyOwnership(const QString &path, const UnixAccount &account, bool recursive, QString *error = nullptr);
};

}
