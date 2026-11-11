#pragma once

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_tostring.hpp>

#include "mikserstudio/core/Project.h"

#include <QByteArray>
#include <QHttpServer>
#include <QHttpServerResponse>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QTcpServer>
#include <QUrl>

#include <chrono>
#include <functional>
#include <memory>

template <>
struct Catch::StringMaker<QString> {
    static std::string convert(const QString& value) { return '"' + value.toStdString() + '"'; }
};

template <>
struct Catch::StringMaker<QJsonObject> {
    static std::string convert(const QJsonObject& value)
    {
        return QJsonDocument(value).toJson(QJsonDocument::Compact).toStdString();
    }
};

namespace mikserstudio::tests {

// Runs the event loop until `done` returns true or the timeout passes; returns `done()`.
bool waitFor(const std::function<bool()>& done, std::chrono::milliseconds timeout = std::chrono::seconds(15));

// Runs the event loop for `duration`.
void spin(std::chrono::milliseconds duration);

// True when nothing accepts TCP connections on 127.0.0.1:<port> any more.
bool portIsClosed(int port);

// Every existing .png is a still image, every other existing file a 90-frame 1080p video with
// audio. Lets the services tests import media without the MLT engine.
class FakeProbe final : public core::MediaProbe {
public:
    std::expected<core::MediaInfo, QString> probe(const QString& path, core::Rational frameRate) override;
};

struct RecordedRequest {
    QByteArray method;  // "GET", "POST", "DELETE"
    QString path;       // Percent-encoded, as sent.
    QByteArray authorization;
    QByteArray body;
};

// An in-process HTTP server on an ephemeral loopback port that answers every request with
// `handler` and records what it received.
class FakeHttpServer {
public:
    using Handler = std::function<QHttpServerResponse(const RecordedRequest&)>;

    FakeHttpServer();
    ~FakeHttpServer();

    void setHandler(Handler handler) { m_handler = std::move(handler); }
    [[nodiscard]] QUrl url() const;
    [[nodiscard]] int port() const { return m_port; }
    [[nodiscard]] const QList<RecordedRequest>& requests() const { return m_requests; }

private:
    std::unique_ptr<QHttpServer> m_server;
    int m_port = 0;
    Handler m_handler;
    QList<RecordedRequest> m_requests;
};

}  // namespace mikserstudio::tests
