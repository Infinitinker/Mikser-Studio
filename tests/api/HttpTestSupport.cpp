// Sending requests to the local API and waiting for their replies.
#include "HttpTestSupport.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <chrono>

using namespace Qt::StringLiterals;

namespace mikserstudio::tests {

namespace {

constexpr auto kRequestTimeout = std::chrono::seconds(10);

}  // namespace

QString HttpReply::errorCode() const
{
    return object().value(u"error"_s).toObject().value(u"code"_s).toString();
}

HttpReply sendRequest(const QByteArray& method, const QUrl& url, const QByteArray& body,
                      const std::optional<QByteArray>& authorization)
{
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setTransferTimeout(kRequestTimeout);
    if (!body.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
    if (authorization)
        request.setRawHeader("Authorization", *authorization);

    QNetworkReply* reply = manager.sendCustomRequest(request, method, body);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(kRequestTimeout + std::chrono::seconds(1), &loop, &QEventLoop::quit);
    if (!reply->isFinished())
        loop.exec();

    HttpReply result;
    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    result.headers = reply->headers();
    if (reply->error() != QNetworkReply::NoError)
        result.networkError = reply->errorString();
    delete reply;
    return result;
}

HttpReply postCommand(const QString& baseUrl, const QString& token, const QString& id, const QJsonObject& params)
{
    return sendRequest("POST", QUrl(baseUrl + u"/v1/commands/"_s + id),
                       QJsonDocument(params).toJson(QJsonDocument::Compact), bearer(token));
}

QByteArray bearer(const QString& token)
{
    return "Bearer " + token.toUtf8();
}

}  // namespace mikserstudio::tests
