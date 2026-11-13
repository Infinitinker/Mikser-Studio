// The "comfyui" backend: images made by the user's ComfyUI, through its HTTP API and its socket.
#pragma once

#include "AiBackend.h"

#include <QString>
#include <QUrl>

class QNetworkAccessManager;

namespace mikserstudio::services {

// Offers "image.generate {prompt, negative_prompt, width, height, steps, seed?}", run as the
// Qwen Image 2.1 workflow (ComfyUiWorkflow.h) in the user's ComfyUI. Sizes are rounded down to a
// multiple of 16; without a seed it picks one and returns it as {seed}. Progress comes from
// ComfyUI's socket. SaveImage writes into "mikserstudio/<job id>" in ComfyUI's output; the images
// are then copied into the job's folder. It never downloads models or installs nodes: without
// them it is unavailable, and the reason names what is missing.
//
// Canceling takes the job's own prompt off ComfyUI's queue (POST /queue {"delete": [id]}); a
// prompt that already runs is left to finish. It never calls /interrupt or /free, which would
// stop or unload whatever ComfyUI is running for someone else.
class ComfyUiBackend final : public AiBackend {
public:
    explicit ComfyUiBackend(QUrl baseUrl, QObject* parent = nullptr);

    [[nodiscard]] QString id() const override;
    [[nodiscard]] QString title() const override;
    [[nodiscard]] QList<AiOperation> operations() const override;
    void setBaseUrl(const QUrl& url) override { m_baseUrl = url; }
    void checkAvailability(const QObject* context, std::function<void(AiAvailability)> done) override;
    [[nodiscard]] std::unique_ptr<AiTask> start(const QString& op, const QJsonObject& params,
                                                AiTaskContext context) override;

private:
    QNetworkAccessManager* m_network;
    QUrl m_baseUrl;
};

}  // namespace mikserstudio::services
