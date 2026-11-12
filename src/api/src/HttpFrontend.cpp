#include "HttpFrontend.h"

#include "ApiResponse.h"
#include "Auth.h"
#include "Logging.h"
#include "Router.h"
#include "mikserstudio/api/ApiServer.h"

#include <QElapsedTimer>
#include <QHttpServerConfiguration>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QMetaEnum>

using namespace Qt::StringLiterals;

namespace mikserstudio::api {

namespace {

// Bodies up to this size are read, so the Router can answer a JSON 413 for those over
// kMaxRequestBodyBytes. Larger ones are refused by Qt with a bare 413 before they are buffered.
constexpr qsizetype kHardBodyLimit = 8 * kMaxRequestBodyBytes;

// The method and path only: the query may carry the token (?token=) and is never logged.
QString describe(const QHttpServerRequest& request)
{
    const QByteArray method = QMetaEnum::fromType<QHttpServerRequest::Method>().valueToKey(
        static_cast<quint64>(request.method()));
    return u"%1 %2"_s.arg(QString::fromLatin1(method).toUpper(), request.url().path());
}

QByteArray compactErrorJson(const QString& code, const QString& message)
{
    return QJsonDocument(errorBody(code, message)).toJson(QJsonDocument::Compact);
}

}  // namespace

HttpFrontend::HttpFrontend(const Router& router, QByteArray token, QObject* parent)
    : QAbstractHttpServer(parent)
    , m_router(router)
    , m_token(std::move(token))
{
    QHttpServerConfiguration config;
    config.setMaximumBodySize(kHardBodyLimit);
    setConfiguration(config);
    addWebSocketUpgradeVerifier(this, [this](const QHttpServerRequest& request) { return verifyUpgrade(request); });
}

bool HttpFrontend::handleRequest(const QHttpServerRequest& request, QHttpServerResponder& responder)
{
    QElapsedTimer timer;
    timer.start();
    ApiResponse response = m_router.handle(request);
    response.headers.append(QHttpHeaders::WellKnownHeader::CacheControl, "no-store");
    responder.write(response.body, response.headers, response.status);
    qCDebug(lcApi).noquote() << describe(request) << "->" << static_cast<int>(response.status) << "in"
                             << timer.elapsed() << "ms";
    return true;
}

void HttpFrontend::missingHandler(const QHttpServerRequest& request, QHttpServerResponder& responder)
{
    // handleRequest() answers every request, so this runs only if Qt bypasses it.
    responder.write(QJsonDocument(errorBody(u"not_found"_s, u"There is no API route %1."_s.arg(request.url().path()))),
                    HttpStatus::NotFound);
}

QHttpServerWebSocketUpgradeResponse HttpFrontend::verifyUpgrade(const QHttpServerRequest& request) const
{
    const QString what = describe(request) + u" (WebSocket)"_s;
    if (request.url().path(QUrl::FullyDecoded) != u"/v1/events"_s) {
        qCDebug(lcApi).noquote() << what << "-> 404";
        return QHttpServerWebSocketUpgradeResponse::deny(
            static_cast<int>(HttpStatus::NotFound),
            compactErrorJson(u"not_found"_s, u"WebSockets are served only on /v1/events."_s));
    }
    if (!isAuthorized(request, m_token, TokenSource::HeaderOrQuery)) {
        qCDebug(lcApi).noquote() << what << "-> 401";
        return QHttpServerWebSocketUpgradeResponse::deny(
            static_cast<int>(HttpStatus::Unauthorized),
            compactErrorJson(
                u"unauthorized"_s,
                u"Missing or wrong API token. Pass ?token=<token> or \"Authorization: Bearer <token>\"."_s));
    }
    qCDebug(lcApi).noquote() << what << "-> 101";
    return QHttpServerWebSocketUpgradeResponse::accept();
}

}  // namespace mikserstudio::api
