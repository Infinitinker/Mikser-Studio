// Bringing what an AI job made into the project: its image, video and audio outputs, with their provenance.
#pragma once

#include <QJsonObject>
#include <QString>

#include <expected>

namespace mikserstudio::core {
class CommandBus;
}  // namespace mikserstudio::core

namespace mikserstudio::services {

// Imports the image, video and audio outputs of a finished job (as AiEngine::jobsJson() lists it)
// with "project.import_media", each with the job's provenance and named `name` (numbered when
// there are several), as one undo step ("Add AI result"). Returns {mediaIds: [...]}; the error
// says which file could not be imported, and the files imported before it stay (the same undo
// step removes them).
[[nodiscard]] std::expected<QJsonObject, QString> importAiOutputs(core::CommandBus& bus, const QJsonObject& job,
                                                                  const QString& name);

}  // namespace mikserstudio::services
