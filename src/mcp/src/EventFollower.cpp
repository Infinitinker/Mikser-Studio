// Opening the events socket with the discovery file's token, reading its events, and trying again.
#include "EventFollower.h"

#include "Discovery.h"
#include "Logging.h"

#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkRequest>

namespace mikserstudio::mcp {

EventFollower::EventFollower(QString discoveryPath, std::chrono::milliseconds retry, QObject* parent)
    : QObject(parent)
    , m_discoveryPath(std::move(discoveryPath))
{
    // The app listens on loopback only; a system proxy must never see its token.
    m_socket.setProxy(QNetworkProxy::NoProxy);
    m_retry.setSingleShot(true);
    m_retry.setInterval(retry);
    connect(&m_retry, &QTimer::timeout, this, &EventFollower::tryToConnect);
    connect(&m_socket, &QWebSocket::connected, this, [this] {
        m_connected = true;
        Q_EMIT connected();
    });
    connect(&m_socket, &QWebSocket::textMessageReceived, this, &EventFollower::receive);
    connect(&m_socket, &QWebSocket::stateChanged, this, [this](QAbstractSocket::SocketState state) {
        if (state == QAbstractSocket::UnconnectedState)
            socketGone();
    });
    connect(&m_socket, &QWebSocket::errorOccurred, this, [this] {
        if (m_socket.state() == QAbstractSocket::UnconnectedState)
            socketGone();
    });
}

void EventFollower::start()
{
    tryToConnect();
}

void EventFollower::tryToConnect()
{
    if (m_socket.state() != QAbstractSocket::UnconnectedState)
        return;
    const std::optional<Discovery> discovery = readDiscovery(m_discoveryPath);
    if (!discovery) {
        m_retry.start();
        return;
    }
    QNetworkRequest request(discovery->eventsUrl);
    request.setRawHeader("Authorization", "Bearer " + discovery->token);
    m_socket.open(request);
}

void EventFollower::socketGone()
{
    if (m_connected) {
        m_connected = false;
        Q_EMIT disconnected();
    }
    if (!m_retry.isActive())
        m_retry.start();
}

void EventFollower::receive(const QString& message)
{
    const QJsonDocument event = QJsonDocument::fromJson(message.toUtf8());
    if (!event.isObject()) {
        qCWarning(lcMcp) << "Ignored an event that is not a JSON object.";
        return;
    }
    Q_EMIT eventReceived(event.object());
}

}  // namespace mikserstudio::mcp
