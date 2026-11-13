// Small asynchronous HTTP requests to an AI tool's API: JSON in, JSON or bytes out, with a timeout.
#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

#include <chrono>
#include <expected>
#include <functional>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;
class QObject;

namespace mikserstudio::services {

using BytesCallback = std::function<void(std::expected<QByteArray, QString>)>;
using JsonCallback = std::function<void(std::expected<QJsonDocument, QString>)>;

// `path` appended to the base URL's own path ("http://host:8188" + "/prompt").
[[nodiscard]] QUrl toolEndpoint(const QUrl& baseUrl, const QString& path, const QUrlQuery& query = {});

// A request that sends `body` as JSON when there is one. Gives up when no data moves for
// `timeout`. `done` runs once, while `context` is alive, with the body of a 2xx answer or an
// English reason: the network error, or the tool's own error message and the HTTP status. Returns
// the reply, which `abort()` stops (`done` then gets an error).
QNetworkReply* sendRequest(QNetworkAccessManager& network, const QByteArray& verb, const QUrl& url,
                           const std::optional<QJsonDocument>& body, std::chrono::milliseconds timeout,
                           const QObject* context, BytesCallback done);

// sendRequest() with the answer parsed as JSON.
QNetworkReply* sendJsonRequest(QNetworkAccessManager& network, const QByteArray& verb, const QUrl& url,
                               const std::optional<QJsonDocument>& body, std::chrono::milliseconds timeout,
                               const QObject* context, JsonCallback done);

// The tool's message in an error answer ({"error": "..."} or {"error": {"message": "..."}}), else
// "HTTP <status>" and the start of the body.
[[nodiscard]] QString errorFromAnswer(int status, const QByteArray& body);

}  // namespace mikserstudio::services
