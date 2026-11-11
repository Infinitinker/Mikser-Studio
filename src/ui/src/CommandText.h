#pragma once

#include <QJsonObject>
#include <QString>

namespace mikserstudio::core {
class CommandBus;
}

namespace mikserstudio::ui {

// The command's title, or its id when it is not registered (any more).
[[nodiscard]] QString commandTitle(const core::CommandBus& bus, const QString& id);
// Compact single-line JSON, cut with "…" after `maxChars` characters; "" for an empty object.
[[nodiscard]] QString compactJson(const QJsonObject& object, qsizetype maxChars);
// What a successful command reports in one short line: its "text" or "message" when it has one,
// otherwise the compact result, or "Done".
[[nodiscard]] QString resultSummary(const QJsonObject& result, qsizetype maxChars);

}  // namespace mikserstudio::ui
