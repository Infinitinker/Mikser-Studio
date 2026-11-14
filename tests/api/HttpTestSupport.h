// HTTP requests to the local API, for its in-process tests and the end-to-end tests.
#pragma once

#include <QByteArray>
#include <QHttpHeaders>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QUrl>

#include <optional>

namespace mikserstudio::tests {

struct HttpReply {
    int status = 0;  // 0 when no HTTP response arrived.
    QByteArray body;
    QHttpHeaders headers;
    QString networkError;

    [[nodiscard]] QJsonObject object() const { return QJsonDocument::fromJson(body).object(); }
    [[nodiscard]] QJsonArray array() const { return QJsonDocument::fromJson(body).array(); }
    [[nodiscard]] QString errorCode() const;
};

// Sends one request with a fresh QNetworkAccessManager and waits for the reply, running the
// thread's event loop meanwhile. `authorization` is the full header value; nullopt sends no
// header.
HttpReply sendRequest(const QByteArray& method, const QUrl& url, const QByteArray& body = {},
                      const std::optional<QByteArray>& authorization = std::nullopt);

// Runs a command: a POST of `params` (`{}` when there are none) to <baseUrl>/v1/commands/<id>,
// with the session token.
HttpReply postCommand(const QString& baseUrl, const QString& token, const QString& id, const QJsonObject& params = {});

// The Authorization header value for the session token.
[[nodiscard]] QByteArray bearer(const QString& token);

}  // namespace mikserstudio::tests
