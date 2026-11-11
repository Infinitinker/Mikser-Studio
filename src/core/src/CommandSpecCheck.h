#pragma once

#include "mikserstudio/core/Command.h"

#include <QString>

#include <expected>

namespace mikserstudio::core {

// True for dotted lower_snake ids with at least two segments ("timeline.add_clip"). Used for
// command ids and plugin ids, so every plugin command id can start with its plugin id.
[[nodiscard]] bool isValidDottedId(const QString& id);

// Checks a spec before registration and fills an empty paramsSchema with a schema that accepts
// only the empty object. Returns the reason in English when the spec is malformed.
[[nodiscard]] std::expected<CommandSpec, QString> normalizeCommandSpec(CommandSpec spec);

}  // namespace mikserstudio::core
