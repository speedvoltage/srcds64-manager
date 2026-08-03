#include "core/game_catalog.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QResource>

static void initializeResources()
{
    static const bool initialized = [] {
        Q_INIT_RESOURCE(resources);
        return true;
    }();
    Q_UNUSED(initialized);
}

namespace srcds64 {

QList<GameDefinition> GameCatalog::all(QString *error)
{
    initializeResources();
    QFile file(QStringLiteral(":/games.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("Unable to open the embedded game catalog.");
        }
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error) {
            *error = QStringLiteral("The embedded game catalog is invalid: %1").arg(parseError.errorString());
        }
        return {};
    }

    QList<GameDefinition> games;
    const QJsonArray entries = document.array();
    games.reserve(entries.size());

    for (const QJsonValue &value : entries) {
        const QJsonObject object = value.toObject();
        GameDefinition game;
        game.id = object.value(QStringLiteral("id")).toString();
        game.name = object.value(QStringLiteral("name")).toString();
        game.appId = static_cast<quint32>(object.value(QStringLiteral("appId")).toInteger());
        game.gameDirectory = object.value(QStringLiteral("gameDirectory")).toString();
        game.defaultMap = object.value(QStringLiteral("defaultMap")).toString();

        if (game.id.isEmpty() || game.name.isEmpty() || game.appId == 0 || game.gameDirectory.isEmpty() || game.defaultMap.isEmpty()) {
            if (error) {
                *error = QStringLiteral("The embedded game catalog contains an incomplete entry.");
            }
            return {};
        }

        games.push_back(game);
    }

    return games;
}

std::optional<GameDefinition> GameCatalog::find(QStringView id, QString *error)
{
    const QList<GameDefinition> games = all(error);
    for (const GameDefinition &game : games) {
        if (QStringView(game.id).compare(id, Qt::CaseInsensitive) == 0) {
            return game;
        }
    }

    if (error && error->isEmpty()) {
        *error = QStringLiteral("Unsupported game: %1").arg(id);
    }
    return std::nullopt;
}

}
