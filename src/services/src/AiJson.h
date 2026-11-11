#pragma once

#include "mikserstudio/services/AiTypes.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <expected>

namespace mikserstudio::services {

// Converts the sidecar's JSON (docs/ai.md) to the C++ types and back. Parse errors are English
// messages that name the field.

[[nodiscard]] std::expected<QList<AiBackendInfo>, QString> parseBackends(const QJsonDocument& document);
[[nodiscard]] std::expected<AiJobInfo, QString> parseJob(const QJsonObject& object);
[[nodiscard]] std::expected<QList<AiJobInfo>, QString> parseJobs(const QJsonDocument& document);

[[nodiscard]] QJsonObject toJson(const AiBackendInfo& backend);

}  // namespace mikserstudio::services
