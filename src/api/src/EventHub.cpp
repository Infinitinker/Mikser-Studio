// Sending the app's events to the clients of /v1/events, and dropping clients that do not keep
// up.
#include "EventHub.h"

#include "Logging.h"
#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/EventBus.h"

#include <QJsonDocument>
#include <QWebSocket>

#include <utility>

using namespace Qt::StringLiterals;

namespace mikserstudio::api {

namespace {

// A client that lets this much unsent data pile up is not reading; it is disconnected.
constexpr qint64 kMaxPendingBytes = qint64{8} * 1024 * 1024;
// Clients have nothing to say on this socket; keep what they can make us buffer small.
constexpr quint64 kMaxIncomingMessageBytes = 4096;

}  // namespace

EventHub::EventHub(core::CommandBus& bus, QObject* parent)
    : QObject(parent)
{
    connect(&bus.events(), &core::EventBus::published, this,
            [this](const core::Event& event) { broadcast(core::toJson(event)); });
}

EventHub::~EventHub()
{
    closeAll();
}

void EventHub::addClient(std::unique_ptr<QWebSocket> socket)
{
    QWebSocket* client = socket.release();
    client->setParent(this);
    client->setMaxAllowedIncomingMessageSize(kMaxIncomingMessageBytes);
    client->setMaxAllowedIncomingFrameSize(kMaxIncomingMessageBytes);
    connect(client, &QWebSocket::disconnected, this, [this, client] {
        m_clients.removeOne(client);
        client->deleteLater();
        qCDebug(lcApi) << "Events client disconnected," << m_clients.size() << "left";
    });
    m_clients.append(client);
    qCDebug(lcApi) << "Events client connected," << m_clients.size() << "connected";
}

void EventHub::closeAll()
{
    const QList<QWebSocket*> clients = std::exchange(m_clients, {});
    for (QWebSocket* client : clients) {
        client->disconnect(this);
        client->close(QWebSocketProtocol::CloseCodeGoingAway, u"Mikser Studio API stopped"_s);
        client->flush();
        delete client;
    }
}

void EventHub::broadcast(const QJsonObject& event)
{
    if (m_clients.isEmpty())
        return;
    const QString message = QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact));
    const QList<QWebSocket*> clients = m_clients;
    for (QWebSocket* client : clients) {
        if (client->bytesToWrite() > kMaxPendingBytes) {
            qCWarning(lcApi) << "Disconnecting an events client that does not read its events";
            client->abort();
            continue;
        }
        client->sendTextMessage(message);
    }
}

}  // namespace mikserstudio::api
