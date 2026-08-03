#pragma once

#include "core/account.h"
#include "core/game_catalog.h"

#include <QList>
#include <QString>

namespace srcds64 {

struct VerificationCheck {
    bool passed;
    QString name;
    QString detail;
};

struct VerificationReport {
    QList<VerificationCheck> checks;

    bool passed() const;
};

class Verifier {
public:
    VerificationReport verify(const GameDefinition &game, const QString &installDirectory, const UnixAccount &account) const;
};

}
