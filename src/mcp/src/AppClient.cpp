// Requests to the app's /v1 routes, and turning their answers and failures into results.
#include "AppClient.h"

#include "Discovery.h"

#include <QJsonDocument>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace mikserstudio::mcp {

namespace {

// Listing is quick. Commands that start AI jobs answer at once, but importing media probes the file first.
constexpr int kListTimeoutMs = 10'000;
constexpr int kRunTimeoutMs = 120'000;

QNetworkRequest request(const Discovery& discovery, const QString& path, int timeoutMs)
{
    QNetworkRequest request(QUrl(discovery.baseUrl.toString() + path));
    request.setRawHeader("Authorization", "Bearer " + discovery.token);
    request.setTransferTimeout(timeoutMs);
    return request;
}

bool timedOut(const QNetworkReply& reply)
{
    return reply.error() == QNetworkReply::OperationCanceledError || reply.error() == QNetworkReply::TimeoutError;
}

int statusOf(const QNetworkReply& reply)
{
    return reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

}  // namespace

QString notRunningMessage()
{
    return u"Mikser Studio is not running, or its local API is off. Start Mikser Studio, then try again."_s;
}

AppClient::AppClient(QString discoveryPath)
    : m_discoveryPath(std::move(discoveryPath))
{
    // The app listens on loopback only; a system proxy must never see its token.
    m_network.setProxy(QNetworkProxy::NoProxy);
}

void AppClient::commands(QObject* context, std::function<void(Commands)> done)
{
    const std::optional<Discovery> discovery = readDiscovery(m_discoveryPath);
    if (!discovery) {
        done(std::unexpected(notRunningMessage()));
        return;
    }
    QNetworkReply* reply = m_network.get(request(*discovery, u"/v1/commands"_s, kListTimeoutMs));
    reply->setParent(context);
    QObject::connect(reply, &QNetworkReply::finished, context, [reply, done = std::move(done)] {
        reply->deleteLater();
        const int status = statusOf(*reply);
        if (status == 0) {
            done(std::unexpected(notRunningMessage()));
            return;
        }
        if (status != 200) {
            done(std::unexpected(u"Mikser Studio did not list its commands (HTTP %1)."_s.arg(status)));
            return;
        }
        const QJsonDocument listed = QJsonDocument::fromJson(reply->readAll());
        if (!listed.isArray()) {
            done(std::unexpected(u"Mikser Studio sent a command list this bridge cannot read."_s));
            return;
        }
        done(listed.array());
    });
}

void AppClient::run(QObject* context, const QString& commandId, const QJsonObject& params,
                    std::function<void(Outcome)> done)
{
    const std::optional<Discovery> discovery = readDiscovery(m_discoveryPath);
    if (!discovery) {
        done(std::unexpected(notRunningMessage()));
        return;
    }
    const QString path = u"/v1/commands/"_s + QString::fromUtf8(QUrl::toPercentEncoding(commandId, "."));
    QNetworkRequest post = request(*discovery, path, kRunTimeoutMs);
    post.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
    QNetworkReply* reply = m_network.post(post, QJsonDocument(params).toJson(QJsonDocument::Compact));
    reply->setParent(context);
    QObject::connect(reply, &QNetworkReply::finished, context, [reply, commandId, done = std::move(done)] {
        reply->deleteLater();
        const int status = statusOf(*reply);
        if (status == 0) {
            done(std::unexpected(timedOut(*reply)
                                     ? u"Mikser Studio did not answer “%1” within two minutes."_s.arg(commandId)
                                     : notRunningMessage()));
            return;
        }
        const QJsonDocument body = QJsonDocument::fromJson(reply->readAll());
        if (!body.isObject()) {
            done(std::unexpected(u"Mikser Studio sent an answer this bridge cannot read."_s));
            return;
        }
        if (status == 200) {
            const QJsonValue result = body.object().value(u"result"_s);
            done(result.isObject() ? result.toObject() : QJsonObject{{u"result"_s, result}});
            return;
        }
        const QJsonValue refused = body.object().value(u"error"_s);
        if (refused.isObject()) {
            done(CommandError{.code = refused[u"code"_s].toString(u"failed"_s),
                              .message = refused[u"message"_s].toString()});
            return;
        }
        done(CommandError{.code = u"failed"_s, .message = u"Mikser Studio answered HTTP %1."_s.arg(status)});
    });
}

}  // namespace mikserstudio::mcp
