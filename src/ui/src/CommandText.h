// Short texts about commands for the UI: a command's title, whether it is quiet, and the one-line
// summary of its result.
#pragma once

#include <QJsonObject>
#include <QString>

namespace mikserstudio::core {
class CommandBus;
}

namespace mikserstudio::ui {

// Whether the command log and the status bar leave the command out: a read-only command, such as
// the "ai.jobs" polls, or a quiet one, such as the many seeks of scrubbing the playhead
// (CommandSpec::readOnly and CommandSpec::quiet).
[[nodiscard]] bool isQuietCommand(const core::CommandBus& bus, const QString& id);
// The command's title, or its id when it is not registered (any more).
[[nodiscard]] QString commandTitle(const core::CommandBus& bus, const QString& id);
// Compact single-line JSON, cut with "…" after `maxChars` characters; "" for an empty object.
[[nodiscard]] QString compactJson(const QJsonObject& object, qsizetype maxChars);
// What a successful command reports in one short line: its "text" or "message" when it has one,
// otherwise the compact result, or "Done".
[[nodiscard]] QString resultSummary(const QJsonObject& result, qsizetype maxChars);

}  // namespace mikserstudio::ui
