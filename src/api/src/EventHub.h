// The /v1/events WebSocket of the local API: pushes command bus and undo stack changes to its clients.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>

#include <memory>

class QWebSocket;

namespace mikserstudio::core {
class CommandBus;
struct CommandError;
}  // namespace mikserstudio::core

namespace mikserstudio::api {

// Pushes CommandBus and UndoStack changes as JSON text messages to every connected
// /v1/events client. The sockets are push-only: messages sent by clients are ignored.
class EventHub final : public QObject {
    Q_OBJECT

public:
    explicit EventHub(core::CommandBus& bus, QObject* parent = nullptr);
    ~EventHub() override;

    void addClient(std::unique_ptr<QWebSocket> socket);
    [[nodiscard]] int clientCount() const;
    // Closes every client with "going away".
    void closeAll();

private:
    void broadcast(const QJsonObject& event);
    void onExecuted(const QString& id, const QJsonObject& params, const QJsonObject& result);
    void onFailed(const QString& id, const QJsonObject& params, const core::CommandError& error);
    void onUndoChanged();

    core::CommandBus& m_bus;
    QList<QWebSocket*> m_clients;
};

}  // namespace mikserstudio::api
