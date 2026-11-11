#pragma once

#include "mikserstudio/core/Project.h"

#include <QJsonObject>
#include <QList>
#include <QString>

#include <expected>

namespace mikserstudio::core {

// The parts of a project document, read from JSON that passed the project schema.
struct ProjectDocument {
    Rational frameRate;
    int width = 0;
    int height = 0;
    QList<MediaItem> media;
    QList<Track> tracks;
    QList<Clip> clips;
};

[[nodiscard]] QJsonObject rationalToJson(Rational rate);
[[nodiscard]] QJsonObject mediaToJson(const MediaItem& item);
[[nodiscard]] QJsonObject trackToJson(const Track& track);
[[nodiscard]] QJsonObject clipToJson(const Clip& clip);

// Validates a current-format document against the project schema and converts it. Media paths
// must pass isAllowedMediaPath(). Does not check references or overlaps; ProjectModel does that
// while loading.
[[nodiscard]] std::expected<ProjectDocument, QString> projectFromJson(const QJsonObject& json);

}  // namespace mikserstudio::core
