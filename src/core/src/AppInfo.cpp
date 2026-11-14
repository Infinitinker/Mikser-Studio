// The app.info command, from the application object's name and version.
#include "mikserstudio/core/AppInfo.h"

#include "mikserstudio/core/CommandBus.h"

#include <QCoreApplication>
#include <QJsonObject>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

void registerAppInfoCommand(CommandBus& bus)
{
    bus.registerCommand(
        CommandSpec{
            .id = u"app.info"_s,
            .title = u"About Mikser Studio"_s,
            .description = u"Names the Mikser Studio that runs (Mikser Studio, Mikser Studio Dev or Mikser Studio E2E) "
                           u"and its version."_s,
            .category = u"App"_s,
            .paramsSchema = {},
            .shortcut = {},
            .undoable = false,
            .readOnly = true,
            .confirmation = {},
            .confirmationParam = {},
            .owner = u"core"_s,
            .resultSchema = objectResultSchema({{u"name"_s, u"string"_s}, {u"version"_s, u"string"_s}}),
        },
        [](const QJsonObject&) -> CommandResult {
            return CommandOutcome{QJsonObject{{u"name"_s, QCoreApplication::applicationName()},
                                              {u"version"_s, QCoreApplication::applicationVersion()}},
                                  nullptr};
        });
}

}  // namespace mikserstudio::core
