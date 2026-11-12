#include "mikserstudio/services/AiClient.h"

#include "AiJson.h"
#include "Logging.h"

#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace mikserstudio::services {

namespace {

const QString kNotRunning = u"The AI service is not running."_s;

QString errorFromBody(const QByteArray& body)
{
    const QJsonObject error = QJsonDocument::fromJson(body).object().value(u"error").toObject();
    return error.value(u"message").toString();
}

template <typename T>
AiClient::Callback<QJsonDocument> adapt(AiClient::Callback<T> done,
                                        std::function<std::expected<T, QString>(const QJsonDocument&)> parse)
{
    return [done = std::move(done), parse = std::move(parse)](std::expected<QJsonDocument, QString> answer) {
        if (!answer) {
            done(std::unexpected(answer.error()));
            return;
        }
        auto parsed = parse(*answer);
        if (!parsed) {
            done(std::unexpected(
                u"The AI service sent an answer Mikser Studio does not understand: %1."_s.arg(parsed.error())));
            return;
        }
        done(std::move(*parsed));
    };
}

std::expected<AiJobInfo, QString> parseJobDocument(const QJsonDocument& document)
{
    return parseJob(document.object());
}

}  // namespace

AiClient::AiClient(QObject* parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    // The sidecar is on loopback; a system proxy must never see its token.
    m_network->setProxy(QNetworkProxy::NoProxy);
}

AiClient::~AiClient() = default;

void AiClient::setEndpoint(const QUrl& baseUrl, const QByteArray& token)
{
    m_baseUrl = baseUrl;
    m_token = token;
}

void AiClient::clearEndpoint()
{
    m_baseUrl.clear();
    m_token.clear();
}

void AiClient::health(const QObject* context, Callback<QJsonObject> done)
{
    send("GET", u"/v1/health"_s, nullptr, context,
         adapt<QJsonObject>(std::move(done), [](const QJsonDocument& document) -> std::expected<QJsonObject, QString> {
             if (!document.isObject())
                 return std::unexpected(u"the health answer is not a JSON object"_s);
             return document.object();
         }));
}

void AiClient::backends(const QObject* context, Callback<QList<AiBackendInfo>> done)
{
    send("GET", u"/v1/backends"_s, nullptr, context, adapt<QList<AiBackendInfo>>(std::move(done), parseBackends));
}

void AiClient::submitJob(const QString& op, const QJsonObject& params, const QObject* context, Callback<QString> done)
{
    const QJsonObject body{{u"op"_s, op}, {u"params"_s, params}};
    send("POST", u"/v1/jobs"_s, &body, context,
         adapt<QString>(std::move(done), [](const QJsonDocument& document) -> std::expected<QString, QString> {
             QString id = document.object().value(u"jobId").toString();
             if (id.isEmpty())
                 return std::unexpected(u"the answer has no \"jobId\""_s);
             return id;
         }));
}

void AiClient::jobs(const QObject* context, Callback<QList<AiJobInfo>> done)
{
    send("GET", u"/v1/jobs"_s, nullptr, context, adapt<QList<AiJobInfo>>(std::move(done), parseJobs));
}

void AiClient::job(const QString& id, const QObject* context, Callback<AiJobInfo> done)
{
    send("GET", u"/v1/jobs/"_s + QString::fromLatin1(QUrl::toPercentEncoding(id)), nullptr, context,
         adapt<AiJobInfo>(std::move(done), parseJobDocument));
}

void AiClient::cancelJob(const QString& id, const QObject* context, Callback<AiJobInfo> done)
{
    send("DELETE", u"/v1/jobs/"_s + QString::fromLatin1(QUrl::toPercentEncoding(id)), nullptr, context,
         adapt<AiJobInfo>(std::move(done), parseJobDocument));
}

void AiClient::send(const QByteArray& verb, const QString& path, const QJsonObject* body, const QObject* context,
                    RawCallback done)
{
    if (!hasEndpoint()) {
        QTimer::singleShot(0, context, [done = std::move(done)] { done(std::unexpected(kNotRunning)); });
        return;
    }

    QUrl url = m_baseUrl;
    url.setPath(path, QUrl::StrictMode);
    QNetworkRequest request(url);
    request.setTransferTimeout(static_cast<int>(m_timeout.count()));
    request.setRawHeader("Authorization", "Bearer " + m_token);
    request.setRawHeader("Accept", "application/json");
    QByteArray payload;
    if (body) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
        payload = QJsonDocument(*body).toJson(QJsonDocument::Compact);
    }

    QNetworkReply* reply = m_network->sendCustomRequest(request, verb, payload);
    const QString seconds = QString::number(static_cast<double>(m_timeout.count()) / 1000.0);
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    connect(reply, &QNetworkReply::finished, context, [reply, verb, path, seconds, done = std::move(done)] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        const QString what = QString::fromLatin1(verb) + u' ' + path;
        if (status == 0) {
            const bool timedOut = reply->error() == QNetworkReply::TimeoutError ||
                                  reply->error() == QNetworkReply::OperationCanceledError;
            const QString reason = timedOut ? u"no answer within %1 seconds"_s.arg(seconds) : reply->errorString();
            qCDebug(lcAi).noquote() << what << "failed:" << reason;
            done(std::unexpected(u"The AI service did not answer (%1)."_s.arg(reason)));
            return;
        }
        if (status >= 400) {
            QString message = errorFromBody(data);
            if (message.isEmpty())
                message = u"The AI service answered %1 with HTTP %2."_s.arg(what).arg(status);
            qCDebug(lcAi).noquote() << what << "->" << status << message;
            done(std::unexpected(message));
            return;
        }
        QJsonParseError error{};
        QJsonDocument document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError) {
            done(std::unexpected(
                u"The AI service sent an answer Mikser Studio does not understand: %1."_s.arg(error.errorString())));
            return;
        }
        done(std::move(document));
    });
}

}  // namespace mikserstudio::services
