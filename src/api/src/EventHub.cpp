// Turning command bus and undo stack changes into events for the clients of /v1/events, and dropping clients
// that do not keep up.
#include "EventHub.h"

#include "Logging.h"
#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/UndoStack.h"

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
    , m_bus(bus)
{
    connect(&bus, &core::CommandBus::executed, this, &EventHub::onExecuted);
    connect(&bus, &core::CommandBus::failed, this, &EventHub::onFailed);
    connect(&bus, &core::CommandBus::commandRegistered, this,
            [this](const QString& id) { broadcast(QJsonObject{{u"type"_s, u"command.registered"_s}, {u"id"_s, id}}); });
    connect(&bus, &core::CommandBus::commandUnregistered, this, [this](const QString& id) {
        broadcast(QJsonObject{{u"type"_s, u"command.unregistered"_s}, {u"id"_s, id}});
    });
    connect(&bus.undoStack(), &core::UndoStack::changed, this, &EventHub::onUndoChanged);
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

int EventHub::clientCount() const
{
    return static_cast<int>(m_clients.size());
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

void EventHub::onExecuted(const QString& id, const QJsonObject& params, const QJsonObject& result)
{
    broadcast(QJsonObject{
        {u"type"_s, u"command.executed"_s},
        {u"id"_s, id},
        {u"params"_s, params},
        {u"result"_s, result},
    });
}

void EventHub::onFailed(const QString& id, const QJsonObject& params, const core::CommandError& error)
{
    broadcast(QJsonObject{
        {u"type"_s, u"command.failed"_s},
        {u"id"_s, id},
        {u"params"_s, params},
        {u"error"_s, QJsonObject{{u"code"_s, core::toString(error.code)}, {u"message"_s, error.message}}},
    });
}

void EventHub::onUndoChanged()
{
    const core::UndoStack& undo = m_bus.undoStack();
    broadcast(QJsonObject{
        {u"type"_s, u"undo.changed"_s},
        {u"canUndo"_s, undo.canUndo()},
        {u"canRedo"_s, undo.canRedo()},
        {u"undoText"_s, undo.undoText()},
        {u"redoText"_s, undo.redoText()},
    });
}

}  // namespace mikserstudio::api
