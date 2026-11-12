// The local API: the versioned /v1 HTTP routes and the /v1/events WebSocket on 127.0.0.1, which run the same
// commands as the menus and plugins and publish the discovery file.
#pragma once

#include <QObject>
#include <QString>

#include <expected>
#include <memory>

namespace mikserstudio::core {
class CommandBus;
}

namespace mikserstudio::api {

class EventHub;
class HttpFrontend;
class Router;

// Version of the /v1 API: bumped only for breaking changes.
inline constexpr int kApiVersion = 1;
// Request bodies larger than this are rejected with 413.
inline constexpr qsizetype kMaxRequestBodyBytes = qsizetype{1024} * 1024;

struct ApiServerOptions {
    quint16 port = 0;  // 0 picks a free (ephemeral) port.
    QString token;  // Empty: a random token is generated. Otherwise 16+ visible ASCII chars.
    QString discoveryFile;  // Where to write api.json: the app passes core::UserFolders::discoveryFile. Required.
};

// The local, versioned API: /v1 HTTP routes plus the /v1/events WebSocket on the same port.
//
// Listens only on loopback (127.0.0.1, and ::1 when available). Every route except
// GET /v1/health requires "Authorization: Bearer <token>"; the events socket also accepts
// "?token=<token>". Commands run on the CommandBus thread, which must be the thread that calls
// start() and owns this object (the main thread). See docs/api.md.
//
// While running, a discovery file {pid, port, baseUrl, eventsUrl, token, apiVersion} lets local
// tools find the server. It is readable only by the current user and removed by stop().
class ApiServer final : public QObject {
    Q_OBJECT

public:
    using Options = ApiServerOptions;

    explicit ApiServer(core::CommandBus& bus, Options options = {}, QObject* parent = nullptr);
    ~ApiServer() override;  // Calls stop().

    // Starts listening and writes the discovery file. Returns the bound port.
    std::expected<quint16, QString> start();
    // Closes all connections and removes the discovery file (if it is still ours). Idempotent.
    void stop();

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] quint16 port() const;  // 0 while stopped.
    [[nodiscard]] QString token() const;
    [[nodiscard]] QString discoveryFilePath() const;
    [[nodiscard]] QString baseUrl() const;  // "http://127.0.0.1:<port>", empty while stopped.
    [[nodiscard]] QString eventsUrl() const;  // "ws://127.0.0.1:<port>/v1/events", empty while stopped.
    [[nodiscard]] int eventClientCount() const;

private:
    void acceptWebSockets();

    core::CommandBus& m_bus;
    QString m_token;
    QString m_discoveryFile;
    quint16 m_requestedPort = 0;
    quint16 m_port = 0;
    std::unique_ptr<Router> m_router;
    std::unique_ptr<EventHub> m_events;
    std::unique_ptr<HttpFrontend> m_http;
};

}  // namespace mikserstudio::api
