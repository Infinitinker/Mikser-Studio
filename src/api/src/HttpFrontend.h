#pragma once

#include <QAbstractHttpServer>
#include <QByteArray>

namespace mikserstudio::api {

class Router;

// The Qt HTTP server: answers every request through the Router, logs it without secrets and
// accepts authorized WebSocket upgrades on /v1/events (taken via nextPendingWebSocketConnection()).
class HttpFrontend final : public QAbstractHttpServer {
    Q_OBJECT

public:
    HttpFrontend(const Router& router, QByteArray token, QObject* parent = nullptr);

protected:
    bool handleRequest(const QHttpServerRequest& request, QHttpServerResponder& responder) override;
    void missingHandler(const QHttpServerRequest& request, QHttpServerResponder& responder) override;

private:
    [[nodiscard]] QHttpServerWebSocketUpgradeResponse verifyUpgrade(const QHttpServerRequest& request) const;

    const Router& m_router;
    QByteArray m_token;
};

}  // namespace mikserstudio::api
