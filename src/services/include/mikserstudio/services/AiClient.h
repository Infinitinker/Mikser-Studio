#pragma once

#include "mikserstudio/services/AiTypes.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include <chrono>
#include <expected>
#include <functional>

class QNetworkAccessManager;

namespace mikserstudio::services {

// Asynchronous client of the AI sidecar's HTTP API (docs/ai.md). Main thread only.
//
// Every call answers exactly once through its callback, on the main thread, with the parsed
// result or an English error message for the user. The callback runs only while `context` is
// alive (pass the object that owns the callback's captures). Without an endpoint every call
// fails with "The AI service is not running."
class AiClient final : public QObject {
    Q_OBJECT

public:
    template <typename T>
    using Callback = std::function<void(std::expected<T, QString>)>;

    explicit AiClient(QObject* parent = nullptr);
    ~AiClient() override;

    void setEndpoint(const QUrl& baseUrl, const QByteArray& token);
    void clearEndpoint();
    [[nodiscard]] bool hasEndpoint() const { return m_baseUrl.isValid(); }
    [[nodiscard]] QUrl baseUrl() const { return m_baseUrl; }

    // Per-request transfer timeout (default 10 s).
    void setTimeout(std::chrono::milliseconds timeout) { m_timeout = timeout; }

    void health(const QObject* context, Callback<QJsonObject> done);
    void backends(const QObject* context, Callback<QList<AiBackendInfo>> done);
    // Starts a job; the backend is chosen by the sidecar. Returns the sidecar's job id.
    void submitJob(const QString& op, const QJsonObject& params, const QObject* context, Callback<QString> done);
    void jobs(const QObject* context, Callback<QList<AiJobInfo>> done);
    void job(const QString& id, const QObject* context, Callback<AiJobInfo> done);
    // Cancels a queued or running job; answers with its final state.
    void cancelJob(const QString& id, const QObject* context, Callback<AiJobInfo> done);

private:
    using RawCallback = std::function<void(std::expected<QJsonDocument, QString>)>;

    void send(const QByteArray& verb, const QString& path, const QJsonObject* body, const QObject* context,
              RawCallback done);

    QNetworkAccessManager* m_network;
    QUrl m_baseUrl;
    QByteArray m_token;
    std::chrono::milliseconds m_timeout{10'000};
};

}  // namespace mikserstudio::services
