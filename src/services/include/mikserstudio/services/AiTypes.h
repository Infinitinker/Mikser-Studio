// The data of the AI engine: backends, operations, job states and outputs.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringView>

#include <optional>

namespace mikserstudio::services {

enum class AiJobState { Queued, Running, Succeeded, Failed, Canceled };

// "queued", "running", "succeeded", "failed", "canceled".
[[nodiscard]] QString toString(AiJobState state);
[[nodiscard]] std::optional<AiJobState> aiJobStateFromString(QStringView text);
[[nodiscard]] bool isFinished(AiJobState state);

struct AiOperation {
    QString id;  // "text.generate"
    QString title;
    QString description;
    QJsonObject paramsSchema;
};

struct AiBackendInfo {
    QString id;  // "ollama", "comfyui"
    QString title;
    QList<AiOperation> operations;
    bool available = false;
    QString reason;  // Why it is not available and what to do; empty when available.
};

// A file a job made, in its folder of the AI cache.
struct AiOutput {
    QString path;  // Absolute, '/' separators.
    QString kind;  // "image", "video" or "audio".
};

}  // namespace mikserstudio::services
