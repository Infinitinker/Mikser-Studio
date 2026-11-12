// What the media and timeline command files share: the undo step type and the schema snippets for parameters.
#pragma once

#include "mikserstudio/core/Command.h"

#include <QJsonObject>
#include <QString>

#include <expected>
#include <functional>
#include <memory>

namespace mikserstudio::core {

class CommandBus;
class MediaProbe;
class ProjectModel;

void registerMediaCommands(CommandBus& bus, ProjectModel& project, MediaProbe& probe);
void registerTimelineCommands(CommandBus& bus, ProjectModel& project);

// One step of an edit: re-applies or reverts a model change. A failure means the model was
// changed behind the undo stack's back; it is logged and the step is skipped.
using EditStep = std::function<std::expected<void, QString>()>;

[[nodiscard]] std::unique_ptr<UndoableEdit> makeEdit(QString text, EditStep undo, EditStep redo);

// Schema snippets shared by the project commands. Frames are 0..kMaxFrame.
[[nodiscard]] QJsonObject idParam(const QString& description);
[[nodiscard]] QJsonObject frameParam(const QString& description);
[[nodiscard]] QJsonObject objectSchema(const QJsonObject& properties, const QStringList& required);

}  // namespace mikserstudio::core
