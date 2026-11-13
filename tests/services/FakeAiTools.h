// Fake AI tools for the tests: an Ollama and a ComfyUI that answer the parts of their APIs the backends use,
// in-process on ephemeral loopback ports, and record what they were asked.
#pragma once

#include "ServicesTestSupport.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <memory>
#include <vector>

class QWebSocket;

namespace mikserstudio::tests {

// /api/version, /api/tags, /api/show and a streamed /api/generate.
class FakeOllama {
public:
    struct Model {
        QString name;
        QStringList capabilities;
    };

    FakeOllama();

    [[nodiscard]] QUrl url() const { return m_http.url(); }
    [[nodiscard]] const QList<RecordedRequest>& requests() const { return m_http.requests(); }
    // The bodies of the /api/generate requests.
    [[nodiscard]] QList<QJsonObject> generateRequests() const;

    QList<Model> models{{QStringLiteral("vision-only:latest"), {QStringLiteral("vision")}},
                        {QStringLiteral("llama3:latest"), {QStringLiteral("completion")}}};
    // The lines /api/generate streams.
    QList<QByteArray> answer{
        R"({"model":"llama3:latest","response":"<think>Let me see.</think>","done":false})",
        R"({"model":"llama3:latest","response":" Hello","done":false})",
        R"({"model":"llama3:latest","response":" there.","done":true})",
    };

private:
    QHttpServerResponse respond(const RecordedRequest& request) const;

    FakeHttpServer m_http;
};

// /system_stats, /models/<folder>, /object_info/<node>, /prompt, /queue, /history/<id>, /view and the
// /ws socket. A queued prompt runs at once unless `holdPrompts` is set; run() then finishes it.
class FakeComfyUi {
public:
    FakeComfyUi();
    ~FakeComfyUi();
    FakeComfyUi(const FakeComfyUi&) = delete;
    FakeComfyUi& operator=(const FakeComfyUi&) = delete;

    [[nodiscard]] QUrl url() const { return m_http.url(); }
    [[nodiscard]] const QList<RecordedRequest>& requests() const { return m_http.requests(); }
    // The workflows queued with /prompt, by prompt id, in order.
    [[nodiscard]] const QList<std::pair<QString, QJsonObject>>& prompts() const { return m_prompts; }
    // The prompt ids /queue was asked to delete.
    [[nodiscard]] QStringList deleted() const;
    // Whether a request to `path` was made ("/interrupt").
    [[nodiscard]] bool wasAsked(const QString& path) const;

    // Sends the socket events of a whole run of `promptId` and keeps its image for /history.
    void run(const QString& promptId);
    // Sends an execution_error for `promptId`.
    void fail(const QString& promptId, const QString& message);

    bool holdPrompts = false;
    QStringList missingModels;
    QStringList missingNodes;
    // A 1x1 PNG, so the app's media probe takes it.
    QByteArray image = QByteArray::fromBase64(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==");

private:
    QHttpServerResponse respond(const RecordedRequest& request);
    void acceptSockets();
    void broadcast(const QJsonObject& event);

    FakeHttpServer m_http;
    std::vector<std::unique_ptr<QWebSocket>> m_sockets;
    QList<std::pair<QString, QJsonObject>> m_prompts;
    QSet<QString> m_finished;
};

}  // namespace mikserstudio::tests
