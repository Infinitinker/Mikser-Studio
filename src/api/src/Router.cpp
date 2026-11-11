#include "Router.h"

#include "Auth.h"
#include "mikserstudio/api/ApiServer.h"
#include "mikserstudio/core/CommandBus.h"

#include <QHttpServerRequest>
#include <QJsonArray>
#include <QJsonParseError>

using namespace Qt::StringLiterals;

namespace mikserstudio::api {

namespace {

using Method = QHttpServerRequest::Method;

const QString kHealthPath = u"/v1/health"_s;
const QString kCommandsPath = u"/v1/commands"_s;
const QString kCommandPrefix = u"/v1/commands/"_s;
const QString kEventsPath = u"/v1/events"_s;

ApiResponse methodNotAllowed(const QByteArray& allowed)
{
    ApiResponse response = errorResponse(HttpStatus::MethodNotAllowed, u"method_not_allowed"_s,
                                         u"This route accepts only %1."_s.arg(QString::fromLatin1(allowed)));
    response.headers.append(QHttpHeaders::WellKnownHeader::Allow, allowed);
    return response;
}

ApiResponse unauthorized()
{
    ApiResponse response = errorResponse(
        HttpStatus::Unauthorized, u"unauthorized"_s,
        u"Missing or wrong API token. Send \"Authorization: Bearer <token>\" with the token from the "
        "discovery file (api.json)."_s);
    response.headers.append(QHttpHeaders::WellKnownHeader::WWWAuthenticate, "Bearer");
    return response;
}

ApiResponse notFound(const QString& path)
{
    return errorResponse(HttpStatus::NotFound, u"not_found"_s,
                         u"There is no API route %1. See GET /v1/commands for what is available."_s.arg(path));
}

ApiResponse unknownCommand(const QString& id)
{
    return errorResponse(HttpStatus::NotFound, core::toString(core::CommandErrorCode::UnknownCommand),
                         u"There is no command \"%1\". GET /v1/commands lists the available commands."_s.arg(id));
}

HttpStatus statusFor(core::CommandErrorCode code)
{
    switch (code) {
    case core::CommandErrorCode::UnknownCommand:
        return HttpStatus::NotFound;
    case core::CommandErrorCode::InvalidParams:
        return HttpStatus::BadRequest;
    case core::CommandErrorCode::NotAvailable:
        return HttpStatus::Conflict;
    case core::CommandErrorCode::Failed:
        return HttpStatus::InternalServerError;
    }
    return HttpStatus::InternalServerError;
}

}  // namespace

Router::Router(core::CommandBus& bus, QByteArray token)
    : m_bus(bus)
    , m_token(std::move(token))
{
}

ApiResponse Router::handle(const QHttpServerRequest& request) const
{
    const QString path = request.url().path(QUrl::FullyDecoded);
    if (path == kHealthPath)
        return health(request);
    if (!isAuthorized(request, m_token, TokenSource::HeaderOnly))
        return unauthorized();
    if (path == kCommandsPath)
        return listCommands(request);
    if (path.startsWith(kCommandPrefix)) {
        const QString id = path.sliced(kCommandPrefix.size());
        if (id.isEmpty() || id.contains(u'/'))
            return notFound(path);
        return command(request, id);
    }
    if (path == kEventsPath) {
        ApiResponse response = errorResponse(HttpStatus::UpgradeRequired, u"upgrade_required"_s,
                                             u"/v1/events is a WebSocket. Connect with a WebSocket client."_s);
        response.headers.append(QHttpHeaders::WellKnownHeader::Upgrade, "websocket");
        return response;
    }
    return notFound(path);
}

ApiResponse Router::health(const QHttpServerRequest& request) const
{
    if (request.method() != Method::Get)
        return methodNotAllowed("GET");
    return jsonResponse(QJsonObject{
        {u"name"_s, u"Mikser Studio"_s},
        {u"version"_s, QStringLiteral(MIKSERSTUDIO_VERSION_STRING)},
        {u"apiVersion"_s, kApiVersion},
    });
}

ApiResponse Router::listCommands(const QHttpServerRequest& request) const
{
    if (request.method() != Method::Get)
        return methodNotAllowed("GET");
    QJsonArray commands;
    for (const core::CommandSpec& spec : m_bus.specs())
        commands.append(core::toJson(spec));
    return ApiResponse{HttpStatus::Ok, QJsonDocument(commands), {}};
}

ApiResponse Router::command(const QHttpServerRequest& request, const QString& id) const
{
    if (request.method() == Method::Post)
        return executeCommand(request, id);
    if (request.method() != Method::Get)
        return methodNotAllowed("GET, POST");
    const core::CommandSpec* spec = m_bus.spec(id);
    if (spec == nullptr)
        return unknownCommand(id);
    return jsonResponse(core::toJson(*spec));
}

ApiResponse Router::executeCommand(const QHttpServerRequest& request, const QString& id) const
{
    const QByteArray body = request.body();
    if (body.size() > kMaxRequestBodyBytes) {
        return errorResponse(HttpStatus::PayloadTooLarge, u"payload_too_large"_s,
                             u"The request body is larger than %1 bytes. Pass large data as file paths."_s.arg(
                                 kMaxRequestBodyBytes));
    }
    QJsonObject params;
    if (!body.trimmed().isEmpty()) {
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            return errorResponse(HttpStatus::BadRequest, u"malformed_json"_s,
                                 u"The request body is not valid JSON (%1 at offset %2). Send a JSON object, "
                                 "e.g. {}."_s.arg(parseError.errorString())
                                     .arg(parseError.offset));
        }
        if (!document.isObject()) {
            return errorResponse(HttpStatus::BadRequest, core::toString(core::CommandErrorCode::InvalidParams),
                                 u"The request body must be a JSON object with the command params, e.g. {}."_s);
        }
        params = document.object();
    }

    auto result = m_bus.execute(id, params);
    if (!result) {
        const core::CommandError& error = result.error();
        return errorResponse(statusFor(error.code), core::toString(error.code), error.message);
    }
    return jsonResponse(QJsonObject{{u"result"_s, *result}});
}

}  // namespace mikserstudio::api
