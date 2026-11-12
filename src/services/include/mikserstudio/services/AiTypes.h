#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringView>

#include <optional>

namespace mikserstudio::services {

// The data of the AI sidecar API (see docs/ai.md).

enum class AiJobState { Queued, Running, Succeeded, Failed, Canceled };

// "queued", "running", "succeeded", "failed", "canceled".
[[nodiscard]] QString toString(AiJobState state);
[[nodiscard]] std::optional<AiJobState> aiJobStateFromString(QStringView text);
[[nodiscard]] bool isFinished(AiJobState state);

struct AiOperation {
    QString id;  // "video.upscale"
    QString title;
    QString description;
    QJsonObject paramsSchema;
};

struct AiBackendInfo {
    QString id;  // "builtin", "ollama"
    QString title;
    QList<AiOperation> operations;
    bool available = false;
    QString reason;  // Why it is not available and what to do; empty when available.
};

struct AiJobOutput {
    QString path;  // Absolute, '/' separators.
    QString kind;  // "video", "image", "audio" or "text".
};

struct AiJobInfo {
    QString id;
    QString op;
    QString backend;
    AiJobState state = AiJobState::Queued;
    double progress = 0.0;  // 0..1
    QString message;
    QJsonObject result;  // Whole result object: outputs, provenance and op-specific fields ("text").
    QList<AiJobOutput> outputs;
    QJsonObject provenance;
    QString error;  // Set when state is Failed.
};

}  // namespace mikserstudio::services
