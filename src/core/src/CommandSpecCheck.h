// Checking command specs and dotted ids before a command or plugin is registered.
#pragma once

#include "mikserstudio/core/Command.h"

#include <QString>

#include <expected>

namespace mikserstudio::core {

// True for dotted lower_snake ids with at least two segments ("timeline.add_clip"). Used for
// command ids and plugin ids, so every plugin command id can start with its plugin id.
[[nodiscard]] bool isValidDottedId(const QString& id);

// The name the MCP bridge gives a command's tool: its id with "_" for ".". Two commands whose tool
// names are equal can't both be registered (CommandBus::registerCommand).
[[nodiscard]] QString toolNameOf(const QString& commandId);

// Checks a spec before registration and fills an empty paramsSchema with a schema that accepts
// only the empty object. Returns the reason in English when the spec is malformed:
// - the id is dotted lower_snake, and its tool name has at most 64 characters (MCP's limit) and
//   isn't "recent_events", which the MCP bridge keeps for its own tool;
// - the title isn't empty;
// - a read-only command is neither undoable nor asks first (no confirmation or confirmationParam);
// - paramsSchema is an object schema;
// - confirmationParam names a boolean property of paramsSchema whose default is false.
[[nodiscard]] std::expected<CommandSpec, QString> normalizeCommandSpec(CommandSpec spec);

}  // namespace mikserstudio::core
