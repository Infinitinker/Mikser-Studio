// The /v1/events WebSocket of the local API: pushes the app's events to its clients.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>

#include <memory>

class QWebSocket;

namespace mikserstudio::core {
class CommandBus;
}  // namespace mikserstudio::core

namespace mikserstudio::api {

// Pushes the events of the CommandBus's EventBus as JSON text messages (core::toJson) to every
// connected /v1/events client. The sockets are push-only: messages sent by clients are ignored.
class EventHub final : public QObject {
    Q_OBJECT

public:
    explicit EventHub(core::CommandBus& bus, QObject* parent = nullptr);
    ~EventHub() override;

    void addClient(std::unique_ptr<QWebSocket> socket);
    // Closes every client with "going away".
    void closeAll();

private:
    void broadcast(const QJsonObject& event);

    QList<QWebSocket*> m_clients;
};

}  // namespace mikserstudio::api
