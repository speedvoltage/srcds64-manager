#pragma once

#include <QList>
#include <QString>
#include <QStringView>

#include <optional>

namespace srcds64 {

struct GameDefinition {
    QString id;
    QString name;
    quint32 appId;
    QString gameDirectory;
    QString defaultMap;
};

class GameCatalog {
public:
    static QList<GameDefinition> all(QString *error = nullptr);
    static std::optional<GameDefinition> find(QStringView id, QString *error = nullptr);
};

}
