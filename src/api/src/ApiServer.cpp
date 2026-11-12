// The local API: listening on the loopback addresses, writing and removing the discovery file, and wiring the
// router, the HTTP frontend and the event hub.
#include "mikserstudio/api/ApiServer.h"

#include "Auth.h"
#include "DiscoveryFile.h"
#include "EventHub.h"
#include "HttpFrontend.h"
#include "Logging.h"
#include "Router.h"
#include "mikserstudio/core/CommandBus.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonObject>
#include <QTcpServer>
#include <QThread>
#include <QWebSocket>

using namespace Qt::StringLiterals;

namespace mikserstudio::api {

namespace {

// Listens on one loopback address and hands the socket to the HTTP server.
std::expected<quint16, QString> listenOn(HttpFrontend& http, const QHostAddress& address, quint16 port)
{
    auto* server = new QTcpServer(&http);
    if (!server->listen(address, port)) {
        const QString reason = server->errorString();
        delete server;
        return std::unexpected(reason);
    }
    if (!http.bind(server)) {
        delete server;
        return std::unexpected(u"the HTTP server could not take the socket"_s);
    }
    return server->serverPort();
}

}  // namespace

ApiServer::ApiServer(core::CommandBus& bus, Options options, QObject* parent)
    : QObject(parent)
    , m_bus(bus)
    , m_token(options.token.isEmpty() ? generateToken() : std::move(options.token))
    , m_discoveryFile(std::move(options.discoveryFile))
    , m_requestedPort(options.port)
{
}

ApiServer::~ApiServer()
{
    stop();
}

std::expected<quint16, QString> ApiServer::start()
{
    if (isRunning())
        return std::unexpected(u"The API server is already running on port %1."_s.arg(m_port));
    if (QThread::currentThread() != m_bus.thread() || thread() != m_bus.thread())
        return std::unexpected(u"The API server must be started on the thread that owns the CommandBus."_s);
    if (auto valid = checkToken(m_token); !valid)
        return std::unexpected(valid.error());
    if (m_discoveryFile.isEmpty()) {
        return std::unexpected(
            u"The API has no discovery file to write. Pass its path in ApiServerOptions::discoveryFile."_s);
    }

    const QByteArray token = m_token.toUtf8();
    m_router = std::make_unique<Router>(m_bus, token);
    m_http = std::make_unique<HttpFrontend>(*m_router, token);

    auto ipv4 = listenOn(*m_http, QHostAddress(QHostAddress::LocalHost), m_requestedPort);
    if (!ipv4) {
        const QString& reason = ipv4.error();
        stop();
        return std::unexpected(u"The API server could not listen on 127.0.0.1:%1: %2. Choose another port or "
                               "use 0 for a free one."_s.arg(m_requestedPort)
                                   .arg(reason));
    }
    m_port = *ipv4;
    if (auto ipv6 = listenOn(*m_http, QHostAddress(QHostAddress::LocalHostIPv6), m_port); !ipv6)
        qCDebug(lcApi).noquote() << "Not listening on [::1]:" << m_port << ":" << ipv6.error();

    m_events = std::make_unique<EventHub>(m_bus);
    connect(m_http.get(), &QAbstractHttpServer::newWebSocketConnection, this, &ApiServer::acceptWebSockets);

    const QJsonObject discovery{
        {u"pid"_s, QCoreApplication::applicationPid()},
        {u"port"_s, m_port},
        {u"baseUrl"_s, baseUrl()},
        {u"eventsUrl"_s, eventsUrl()},
        {u"token"_s, m_token},
        {u"apiVersion"_s, kApiVersion},
    };
    if (auto written = writeDiscoveryFile(m_discoveryFile, discovery); !written) {
        stop();
        return std::unexpected(written.error());
    }

    qCInfo(lcApi).noquote() << u"API listening on %1, discovery file %2"_s.arg(
        baseUrl(), QDir::toNativeSeparators(m_discoveryFile));
    return m_port;
}

void ApiServer::stop()
{
    const bool wasRunning = isRunning();
    if (m_events)
        m_events->closeAll();
    m_http.reset();
    m_events.reset();
    m_router.reset();
    m_port = 0;
    if (wasRunning) {
        removeDiscoveryFile(m_discoveryFile, m_token);
        qCInfo(lcApi) << "API stopped";
    }
}

bool ApiServer::isRunning() const
{
    return m_port != 0;
}

quint16 ApiServer::port() const
{
    return m_port;
}

QString ApiServer::token() const
{
    return m_token;
}

QString ApiServer::discoveryFilePath() const
{
    return m_discoveryFile;
}

QString ApiServer::baseUrl() const
{
    return isRunning() ? u"http://127.0.0.1:%1"_s.arg(m_port) : QString();
}

QString ApiServer::eventsUrl() const
{
    return isRunning() ? u"ws://127.0.0.1:%1/v1/events"_s.arg(m_port) : QString();
}

void ApiServer::acceptWebSockets()
{
    while (m_http && m_http->hasPendingWebSocketConnections()) {
        std::unique_ptr<QWebSocket> socket = m_http->nextPendingWebSocketConnection();
        if (socket && m_events)
            m_events->addClient(std::move(socket));
    }
}

}  // namespace mikserstudio::api
