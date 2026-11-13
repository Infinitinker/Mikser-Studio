// The "ollama" backend: text generation with the models of the user's Ollama, through its HTTP API.
#pragma once

#include "AiBackend.h"

#include <QString>
#include <QUrl>

class QNetworkAccessManager;

namespace mikserstudio::services {

// Offers "text.generate {prompt, model?}". Without a model it uses the first installed one whose
// capabilities include "completion". The answer streams from /api/generate; reasoning a thinking
// model writes before "</think>" is dropped. The result is {text, model}; the provenance has
// Ollama's version and the model's name, digest, family, size and quantization. Canceling closes
// the connection, which stops Ollama's answer.
class OllamaBackend final : public AiBackend {
public:
    explicit OllamaBackend(QUrl baseUrl, QObject* parent = nullptr);

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

// The answer without the reasoning some thinking models write before "</think>" when Ollama does
// not separate it for them, trimmed.
[[nodiscard]] QString stripReasoning(const QString& text);

}  // namespace mikserstudio::services
