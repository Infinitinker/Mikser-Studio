// Sending requests to an AI tool and turning its answers and errors into results.
#include "HttpRequests.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>

using namespace Qt::StringLiterals;

namespace mikserstudio::services {

QUrl toolEndpoint(const QUrl& baseUrl, const QString& path, const QUrlQuery& query)
{
    QUrl url = baseUrl;
    QString basePath = baseUrl.path();
    while (basePath.endsWith(u'/'))
        basePath.chop(1);
    url.setPath(basePath + path);
    url.setQuery(query);
    return url;
}

QNetworkReply* sendRequest(QNetworkAccessManager& network, const QByteArray& verb, const QUrl& url,
                           const std::optional<QJsonDocument>& body, std::chrono::milliseconds timeout,
                           const QObject* context, BytesCallback done)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(timeout);
    QByteArray payload;
    if (body) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
        payload = body->toJson(QJsonDocument::Compact);
    }
    QNetworkReply* reply = network.sendCustomRequest(request, verb, payload);
    // Freed when it finishes, also when `context` has gone by then.
    QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    QObject::connect(reply, &QNetworkReply::finished, context, [reply, done = std::move(done)] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray answer = reply->readAll();
        if (status != 0 && (status < 200 || status > 299)) {
            done(std::unexpected(errorFromAnswer(status, answer)));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            const bool timedOut = reply->error() == QNetworkReply::OperationCanceledError ||
                                  reply->error() == QNetworkReply::TimeoutError;
            done(std::unexpected(timedOut ? u"no answer in time"_s : reply->errorString()));
            return;
        }
        done(answer);
    });
    return reply;
}

QNetworkReply* sendJsonRequest(QNetworkAccessManager& network, const QByteArray& verb, const QUrl& url,
                               const std::optional<QJsonDocument>& body, std::chrono::milliseconds timeout,
                               const QObject* context, JsonCallback done)
{
    return sendRequest(network, verb, url, body, timeout, context,
                       [done = std::move(done)](std::expected<QByteArray, QString> answer) {
                           if (!answer) {
                               done(std::unexpected(answer.error()));
                               return;
                           }
                           QJsonParseError error;
                           const QJsonDocument document = QJsonDocument::fromJson(*answer, &error);
                           if (error.error != QJsonParseError::NoError) {
                               done(std::unexpected(u"its answer is not JSON (%1)"_s.arg(error.errorString())));
                               return;
                           }
                           done(document);
                       });
}

QString errorFromAnswer(int status, const QByteArray& body)
{
    const QJsonObject answer = QJsonDocument::fromJson(body).object();
    const QJsonValue error = answer.value(u"error");
    QString message = error.isString() ? error.toString() : error.toObject().value(u"message").toString();
    // ComfyUI names the node it refused the workflow for.
    for (const QJsonValue node : answer.value(u"node_errors").toObject()) {
        for (const QJsonValue detail : node.toObject().value(u"errors").toArray()) {
            const QJsonObject problem = detail.toObject();
            message += u": %1 %2"_s.arg(problem.value(u"message").toString(), problem.value(u"details").toString());
            return message.trimmed();
        }
    }
    if (!message.isEmpty())
        return message;
    return u"HTTP %1: %2"_s.arg(status).arg(QString::fromUtf8(body).trimmed().left(200));
}

}  // namespace mikserstudio::services
