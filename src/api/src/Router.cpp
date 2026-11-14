// The /v1 routes (health, listing commands, running a command): authorization, parsing, calling the command bus
// and turning command errors into HTTP errors.
#include "Router.h"

#include "Auth.h"
#include "mikserstudio/api/ApiServer.h"
#include "mikserstudio/core/CommandBus.h"

#include <QHttpServerRequest>
#include <QJsonArray>
#include <QJsonParseError>
#include <QTimer>

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
                         u"There is no command “%1”. GET /v1/commands lists the available commands."_s.arg(id));
}

HttpStatus statusFor(core::CommandErrorCode code)
{
    switch (code) {
    case core::CommandErrorCode::UnknownCommand:
        return HttpStatus::NotFound;
    case core::CommandErrorCode::InvalidParams:
        return HttpStatus::BadRequest;
    case core::CommandErrorCode::NotAvailable:
    case core::CommandErrorCode::NeedsConfirmation:
    case core::CommandErrorCode::Canceled:
        return HttpStatus::Conflict;
    case core::CommandErrorCode::PermissionDenied:
        return HttpStatus::Forbidden;
    case core::CommandErrorCode::Failed:
        return HttpStatus::InternalServerError;
    }
    return HttpStatus::InternalServerError;
}

// The command id of a /v1/commands/<id> path, or empty for any other path.
QString commandId(const QString& path)
{
    if (!path.startsWith(kCommandPrefix))
        return {};
    const QString id = path.sliced(kCommandPrefix.size());
    return id.contains(u'/') ? QString() : id;
}

// A POST's params: its body, a JSON object, or {} when it is empty.
std::expected<QJsonObject, ApiResponse> paramsOf(const QHttpServerRequest& request)
{
    const QByteArray body = request.body();
    if (body.size() > kMaxRequestBodyBytes) {
        return std::unexpected(errorResponse(
            HttpStatus::PayloadTooLarge, u"payload_too_large"_s,
            u"The request body is larger than %1 bytes. Pass large data as file paths."_s.arg(kMaxRequestBodyBytes)));
    }
    if (body.trimmed().isEmpty())
        return QJsonObject();
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return std::unexpected(errorResponse(HttpStatus::BadRequest, u"malformed_json"_s,
                                             u"The request body is not valid JSON (%1 at offset %2). Send a JSON "
                                             "object, e.g. {}."_s.arg(parseError.errorString())
                                                 .arg(parseError.offset)));
    }
    if (!document.isObject()) {
        return std::unexpected(
            errorResponse(HttpStatus::BadRequest, core::toString(core::CommandErrorCode::InvalidParams),
                          u"The request body must be a JSON object with the command params, e.g. {}."_s));
    }
    return document.object();
}

}  // namespace

Router::Router(core::CommandBus& bus, QByteArray token, std::function<bool()> pluginsReady)
    : m_bus(bus)
    , m_token(std::move(token))
    , m_pluginsReady(std::move(pluginsReady))
{
}

void Router::handle(const QHttpServerRequest& request, QObject* receiver, Answer answer) const
{
    // Only running a command may answer later; every other request answers at once.
    const QString path = request.url().path(QUrl::FullyDecoded);
    if (const QString id = commandId(path);
        !id.isEmpty() && request.method() == Method::Post && isAuthorized(request, m_token, TokenSource::HeaderOnly)) {
        executeCommand(request, id, receiver, std::move(answer));
        return;
    }
    answer(respond(request, path));
}

ApiResponse Router::respond(const QHttpServerRequest& request, const QString& path) const
{
    if (path == kHealthPath)
        return health(request);
    if (!isAuthorized(request, m_token, TokenSource::HeaderOnly))
        return unauthorized();
    if (path == kCommandsPath)
        return listCommands(request);
    if (path.startsWith(kCommandPrefix)) {
        const QString id = commandId(path);
        if (id.isEmpty())
            return notFound(path);
        return describeCommand(request, id);
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
        {u"pluginsReady"_s, m_pluginsReady ? m_pluginsReady() : true},
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

ApiResponse Router::describeCommand(const QHttpServerRequest& request, const QString& id) const
{
    if (request.method() != Method::Get)
        return methodNotAllowed("GET, POST");
    const core::CommandSpec* spec = m_bus.spec(id);
    if (spec == nullptr)
        return unknownCommand(id);
    return jsonResponse(core::toJson(*spec));
}

void Router::executeCommand(const QHttpServerRequest& request, const QString& id, QObject* receiver,
                            Answer answer) const
{
    auto params = paramsOf(request);
    if (!params) {
        answer(std::move(params.error()));
        return;
    }
    const auto run = m_bus.executeAsync(
        id, *params, core::CallContext::api(), receiver,
        [answer = std::move(answer)](const std::expected<QJsonObject, core::CommandError>& result) {
            if (!result) {
                const core::CommandError& error = result.error();
                answer(errorResponse(statusFor(error.code), core::toString(error.code), error.message));
                return;
            }
            answer(jsonResponse(QJsonObject{{u"result"_s, *result}}));
        });
    if (!m_bus.isPending(run))
        return;  // It has answered.
    QTimer::singleShot(kRunTimeout, receiver, [&bus = m_bus, run, id] {
        bus.cancel(run, u"“%1” didn’t finish within %2 seconds, so the API stopped waiting for it. Its work may go on: "
                        u"follow /v1/events to see when it ends."_s.arg(id)
                            .arg(kRunTimeout.count()));
    });
}

}  // namespace mikserstudio::api
