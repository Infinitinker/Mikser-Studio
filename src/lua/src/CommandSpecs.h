// The commands a Lua plugin registers with ms.command: from the table it gives to a CommandSpec.
#pragma once

#include "mikserstudio/core/Command.h"

#include <QJsonObject>
#include <QString>

#include <expected>

namespace mikserstudio::lua {

// The spec of a command plugin `pluginId` registers, from ms.command's table (without its
// handler): {id, title, description?, category?, params?, result?, permission?, read_only?,
// confirmation?, open_world?, quiet?, shortcut?, starts_job?}. The id must start with the plugin's
// id and a dot. `category` defaults to `defaultCategory`. Fails, saying why, for a spec that
// doesn't fit.
[[nodiscard]] std::expected<core::CommandSpec, QString> commandSpecFromJson(const QJsonObject& json,
                                                                            const QString& pluginId,
                                                                            const QString& defaultCategory);

}  // namespace mikserstudio::lua
