// Running http.request and http.stream_ndjson jobs: checking their input and address, sending the
// request, reading the answer or its JSON lines, and stopping when canceled.
#include "mikserstudio/services/HttpJobs.h"

#include "HttpRequests.h"
#include "Logging.h"
#include "NetworkAccess.h"
#include "mikserstudio/core/JobManager.h"
#include "mikserstudio/core/JsonSchema.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>

#include <chrono>
#include <memory>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace mikserstudio::services {

struct HttpJobs::Call {
    QUrl url;
    QByteArray method;
    std::optional<QJsonDocument> body;
    std::chrono::milliseconds timeout{0};
    QStringList fields;
    QJsonObject messages;
};

namespace {

using HttpCall = HttpJobs::Call;

const auto kRequest = u"http.request"_s;
const auto kStream = u"http.stream_ndjson"_s;

QJsonObject inputSchema(bool stream)
{
    QJsonObject properties{
        {u"url"_s, QJsonObject{{u"type"_s, u"string"_s}, {u"minLength"_s, 1}}},
        {u"method"_s, QJsonObject{{u"type"_s, u"string"_s},
                                  {u"enum"_s, QJsonArray{u"GET"_s, u"POST"_s, u"PUT"_s, u"PATCH"_s, u"DELETE"_s}}}},
        {u"body"_s, QJsonObject{{u"type"_s, QJsonArray{u"object"_s, u"array"_s}}}},
        {u"timeout"_s, QJsonObject{{u"type"_s, u"number"_s}, {u"exclusiveMinimum"_s, 0}, {u"maximum"_s, 3600}}},
        {u"title"_s, QJsonObject{{u"type"_s, u"string"_s}}},
    };
    if (stream) {
        properties.insert(u"fields"_s,
                          QJsonObject{{u"type"_s, u"array"_s},
                                      {u"minItems"_s, 1},
                                      {u"items"_s, QJsonObject{{u"type"_s, u"string"_s}, {u"minLength"_s, 1}}}});
        properties.insert(
            u"messages"_s,
            QJsonObject{{u"type"_s, u"object"_s}, {u"additionalProperties"_s, QJsonObject{{u"type"_s, u"string"_s}}}});
    }
    return QJsonObject{{u"type"_s, u"object"_s},
                       {u"properties"_s, properties},
                       {u"required"_s, QJsonArray{u"url"_s}},
                       {u"additionalProperties"_s, false}};
}

HttpCall readCall(const QJsonObject& input, bool stream)
{
    HttpCall call;
    call.url = QUrl(input.value(u"url").toString().trimmed(), QUrl::StrictMode);
    call.method = input.value(u"method").toString(stream ? u"POST"_s : u"GET"_s).toLatin1();
    if (const QJsonValue body = input.value(u"body"); body.isObject())
        call.body = QJsonDocument(body.toObject());
    else if (body.isArray())
        call.body = QJsonDocument(body.toArray());
    const double seconds = input.value(u"timeout").toDouble(stream ? 600.0 : 30.0);
    call.timeout = std::chrono::milliseconds(static_cast<qint64>(seconds * 1000.0));
    for (const QJsonValue field : input.value(u"fields").toArray(QJsonArray{u"response"_s}))
        call.fields.append(field.toString());
    call.messages = input.value(u"messages").toObject();
    return call;
}

QString shown(const QUrl& url)
{
    return url.toString(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment);
}

QString seconds(std::chrono::milliseconds timeout)
{
    return QString::number(static_cast<double>(timeout.count()) / 1000.0);
}

// Why a reply that ended without an HTTP answer failed.
QString unreachable(const QNetworkReply& reply, const HttpCall& call)
{
    const bool timedOut = reply.error() == QNetworkReply::OperationCanceledError ||
                          reply.error() == QNetworkReply::TimeoutError;
    if (timedOut)
        return u"%1 sent nothing for %2 s."_s.arg(shown(call.url), seconds(call.timeout));
    return u"Could not reach %1: %2."_s.arg(shown(call.url), reply.errorString());
}

QString tooLarge(const HttpCall& call)
{
    return u"The answer from %1 is larger than %2 MB."_s.arg(shown(call.url))
        .arg(HttpJobs::kMaxAnswerBytes / (qint64{1024} * 1024));
}

bool isSuccess(int status)
{
    return status >= 200 && status <= 299;
}

// What a stream job has read so far.
struct StreamState {
    QByteArray buffer;
    QJsonObject texts;
    QJsonObject last;
    int lines = 0;
    qint64 received = 0;
    QString error;
    bool overflow = false;
};

}  // namespace

HttpJobs::HttpJobs(core::JobManager& jobs, HttpJobOptions options, QObject* parent)
    : QObject(parent)
    , m_jobs(jobs)
    , m_options(std::move(options))
    , m_network(new QNetworkAccessManager(this))
{
    for (const bool stream : {false, true}) {
        m_jobs.registerKind(
            stream ? kStream : kRequest,
            core::JobKind{
                .permission = u"network"_s,
                .start = [this, stream](core::JobManager& manager, core::JobSpec spec,
                                        const QJsonObject& input) -> std::expected<QString, core::CommandError> {
                    const QString kind = spec.kind;
                    if (const QStringList problems = core::JsonSchema::validate(input, inputSchema(stream));
                        !problems.isEmpty()) {
                        return std::unexpected(
                            core::CommandError{core::CommandErrorCode::InvalidParams,
                                               u"The %1 input doesn’t fit: %2"_s.arg(kind, problems.join(u"; "_s))});
                    }
                    HttpCall call = readCall(input, stream);
                    if (!isWebAddress(call.url)) {
                        return std::unexpected(core::CommandError{
                            core::CommandErrorCode::InvalidParams,
                            u"“%1” is not an http address. Enter one with a host and port, such as http://127.0.0.1:11434."_s
                                .arg(input.value(u"url").toString())});
                    }
                    const auto allowed = checkNetworkAccess(spec.network, call.url,
                                                            m_options.apiPort ? m_options.apiPort() : std::nullopt);
                    if (!allowed)
                        return std::unexpected(
                            core::CommandError{core::CommandErrorCode::PermissionDenied, allowed.error()});
                    spec.title = input.value(u"title").toString(
                        u"%1 %2"_s.arg(QString::fromLatin1(call.method), shown(call.url)));
                    auto shared = std::make_shared<HttpCall>(std::move(call));
                    return manager.submit(
                        std::move(spec), [this, stream, shared](const QString& id) { send(id, shared, stream); },
                        [this](const QString& id) {
                            if (const auto it = m_replies.find(id); it != m_replies.end()) {
                                const QPointer<QNetworkReply> reply = it->second;
                                m_replies.erase(it);
                                if (reply)
                                    reply->abort();
                            }
                        });
                },
            });
    }
}

HttpJobs::~HttpJobs()
{
    m_jobs.unregisterKind(kRequest);
    m_jobs.unregisterKind(kStream);
    const auto replies = std::exchange(m_replies, {});
    for (const auto& [id, reply] : replies) {
        if (reply) {
            reply->disconnect(this);
            reply->abort();
        }
        m_jobs.fail(id, u"The request stopped: Mikser Studio is closing."_s);
    }
}

void HttpJobs::send(const QString& id, const std::shared_ptr<const Call>& call, bool stream)
{
    QNetworkRequest request(call->url);
    request.setTransferTimeout(call->timeout);
    // A redirect could lead anywhere, past the network entries: the plugin gets the 3xx instead.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    QByteArray payload;
    if (call->body) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
        payload = call->body->toJson(QJsonDocument::Compact);
    }
    QNetworkReply* reply = m_network->sendCustomRequest(request, call->method, payload);
    m_replies.emplace(id, reply);
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    if (stream)
        readStream(id, call, reply);
    else
        readAnswer(id, call, reply);
}

void HttpJobs::readAnswer(const QString& id, const std::shared_ptr<const Call>& call, QNetworkReply* reply)
{
    auto overflow = std::make_shared<bool>(false);
    connect(reply, &QNetworkReply::downloadProgress, this, [reply, overflow](qint64 received, qint64) {
        if (received > kMaxAnswerBytes && !*overflow) {
            *overflow = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, id, call, reply, overflow] {
        m_replies.erase(id);
        if (*overflow) {
            m_jobs.fail(id, tooLarge(*call));
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 0) {
            m_jobs.fail(id, unreachable(*reply, *call));
            return;
        }
        const QByteArray body = reply->readAll();
        QJsonObject result{{u"status"_s, status}, {u"body"_s, QString::fromUtf8(body)}};
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(body, &error);
        if (error.error == QJsonParseError::NoError)
            result.insert(u"json"_s, document.isArray() ? QJsonValue(document.array()) : QJsonValue(document.object()));
        m_jobs.succeed(id, result);
    });
}

void HttpJobs::readStream(const QString& id, const std::shared_ptr<const Call>& call, QNetworkReply* reply)
{
    auto state = std::make_shared<StreamState>();
    for (const QString& field : call->fields)
        state->texts.insert(field, QString());
    const auto readLine = [this, id, call, state](const QByteArray& line) {
        const QJsonObject chunk = QJsonDocument::fromJson(line).object();
        if (chunk.contains(u"error")) {
            const QJsonValue error = chunk.value(u"error");
            state->error = error.isObject() ? error.toObject().value(u"message").toString()
                                            : error.toVariant().toString();
            return false;
        }
        ++state->lines;
        state->last = chunk;
        QString grew;
        for (const QString& field : call->fields) {
            const QString text = chunk.value(field).toString();
            if (text.isEmpty())
                continue;
            state->texts.insert(field, state->texts.value(field).toString() + text);
            if (grew.isEmpty())
                grew = field;
        }
        if (const QString message = call->messages.value(grew).toString(); !grew.isEmpty() && !message.isEmpty())
            m_jobs.progress(id, 0.0, QString(message).replace(u"{count}"_s, QString::number(state->lines)));
        return true;
    };
    const auto readLines = [state, readLine, reply] {
        state->buffer += reply->readAll();
        qsizetype end = 0;
        while (state->error.isEmpty() && (end = state->buffer.indexOf('\n')) >= 0) {
            const QByteArray line = state->buffer.left(end).trimmed();
            state->buffer.remove(0, end + 1);
            if (!line.isEmpty() && !readLine(line)) {
                reply->abort();
                return;
            }
        }
    };
    connect(reply, &QNetworkReply::readyRead, this, [reply, state, readLines] {
        if (!state->error.isEmpty() || state->overflow ||
            !isSuccess(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()))
            return;  // An error answer is read whole when it has finished.
        state->received += reply->bytesAvailable();
        if (state->received > kMaxAnswerBytes) {
            state->overflow = true;
            reply->abort();
            return;
        }
        readLines();
    });
    connect(reply, &QNetworkReply::finished, this, [this, id, call, reply, state, readLine, readLines] {
        m_replies.erase(id);
        if (state->overflow) {
            m_jobs.fail(id, tooLarge(*call));
            return;
        }
        if (!state->error.isEmpty()) {
            m_jobs.fail(id, state->error);
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 0 && !isSuccess(status)) {
            m_jobs.fail(id, errorFromAnswer(status, reply->readAll()));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            m_jobs.fail(id, unreachable(*reply, *call));
            return;
        }
        readLines();
        if (const QByteArray rest = state->buffer.trimmed(); state->error.isEmpty() && !rest.isEmpty())
            readLine(rest);
        if (!state->error.isEmpty()) {
            m_jobs.fail(id, state->error);
            return;
        }
        m_jobs.succeed(id,
                       QJsonObject{{u"fields"_s, state->texts}, {u"last"_s, state->last}, {u"lines"_s, state->lines}});
    });
}

}  // namespace mikserstudio::services
