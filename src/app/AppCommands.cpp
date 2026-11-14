// The commands that tie modules together: the project file commands and source.open_media.
#include "AppCommands.h"

#include "ProjectFileCommands.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/engine/ClipPlayer.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace mikserstudio::app {

using core::CommandErrorCode;
using core::commandFailure;

void registerAppCommands(core::CommandBus& bus, core::ProjectModel& project, engine::ClipPlayer* source)
{
    registerProjectFileCommands(bus, project);

    if (!source)
        return;
    const QJsonObject mediaSchema{
        {u"type"_s, u"object"_s},
        {u"properties"_s, QJsonObject{{u"mediaId"_s, QJsonObject{{u"type"_s, u"string"_s}, {u"minLength"_s, 1}}}}},
        {u"required"_s, QJsonArray{u"mediaId"_s}},
        {u"additionalProperties"_s, false},
    };
    bus.registerCommand({.id = u"source.open_media"_s,
                         .title = u"Show in source monitor"_s,
                         .description = u"Loads a media item from the bin into the source monitor."_s,
                         .category = u"Source monitor"_s,
                         .paramsSchema = mediaSchema,
                         .shortcut = {},
                         .undoable = false,
                         .readOnly = false,
                         .confirmation = {},
                         .confirmationParam = {},
                         .owner = u"core"_s,
                         .resultSchema = core::objectResultSchema({{u"mediaId"_s, u"string"_s}}),
                         .permission = u"ui"_s},
                        [&project, source](const QJsonObject& params) -> core::CommandResult {
                            const QString id = params.value(u"mediaId"_s).toString();
                            const core::MediaItem* item = project.findMedia(id);
                            if (!item)
                                return commandFailure(CommandErrorCode::Failed,
                                                      u"There is no media item “%1” in the project."_s.arg(id));
                            if (const auto opened = source->openFile(item->path); !opened)
                                return commandFailure(CommandErrorCode::Failed, opened.error());
                            return core::CommandOutcome{QJsonObject{{u"mediaId"_s, id}}, nullptr};
                        });
}

}  // namespace mikserstudio::app
