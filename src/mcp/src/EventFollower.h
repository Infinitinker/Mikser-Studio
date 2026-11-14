// Following the app's events on /v1/events while it runs, and finding it again after it stops.
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QWebSocket>

#include <chrono>

namespace mikserstudio::mcp {

// Keeps the events socket open while the app runs. Whenever the socket is closed or cannot be opened, it
// tries again after `retry`, reading the discovery file anew, since the app writes a new port and token at
// each start. A socket that is not open after `handshakeTimeout`, such as one to a port a crashed app left in
// its discovery file and another program took, is given up.
class EventFollower : public QObject {
    Q_OBJECT

public:
    EventFollower(QString discoveryPath, std::chrono::milliseconds retry, std::chrono::milliseconds handshakeTimeout,
                  QObject* parent = nullptr);

    void start();

Q_SIGNALS:
    // The socket is open: the app runs.
    void connected();
    // The socket that was open has closed: the app stopped.
    void disconnected();
    void eventReceived(const QJsonObject& event);

private:
    void tryToConnect();
    void socketGone();
    void receive(const QString& message);

    QString m_discoveryPath;
    QWebSocket m_socket;
    QTimer m_retry;
    QTimer m_handshake;
    bool m_connected = false;
};

}  // namespace mikserstudio::mcp
