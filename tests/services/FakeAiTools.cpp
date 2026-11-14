// The fake Ollama and ComfyUI: their answers, ComfyUI's socket events and the records of what they were asked.
#include "FakeAiTools.h"

#include <QHttpServer>
#include <QHttpServerWebSocketUpgradeResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QWebSocket>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mikserstudio::tests {

namespace {

QHttpServerResponse json(const QJsonValue& value)
{
    const QJsonDocument document = value.isArray() ? QJsonDocument(value.toArray()) : QJsonDocument(value.toObject());
    return QHttpServerResponse("application/json", document.toJson(QJsonDocument::Compact));
}

QHttpServerResponse notFound()
{
    return QHttpServerResponse(QHttpServerResponse::StatusCode::NotFound);
}

QJsonObject bodyOf(const RecordedRequest& request)
{
    return QJsonDocument::fromJson(request.body).object();
}

const QStringList kComfyModels{u"qwen_image_2.1_int8_convrot.safetensors"_s, u"qwen3vl_8b_int8_convrot.safetensors"_s,
                               u"qwen_image_2.1_vae_bf16.safetensors"_s};

}  // namespace

FakeOllama::FakeOllama()
{
    m_http.setHandler([this](const RecordedRequest& request) { return respond(request); });
}

QHttpServerResponse FakeOllama::respond(const RecordedRequest& request)
{
    if (request.path.startsWith(u"/fake/"))
        return control(request);
    if (request.path == u"/api/version")
        return json(QJsonObject{{u"version"_s, u"0.12.3"_s}});
    if (request.path == u"/api/tags") {
        QJsonArray list;
        for (const Model& model : models) {
            list.append(QJsonObject{
                {u"name"_s, model.name},
                {u"digest"_s, u"sha256:"_s + model.name},
                {u"details"_s, QJsonObject{{u"family"_s, u"llama"_s},
                                           {u"parameter_size"_s, u"8.0B"_s},
                                           {u"quantization_level"_s, u"Q4_K_M"_s}}},
            });
        }
        return json(QJsonObject{{u"models"_s, list}});
    }
    if (request.path == u"/api/show") {
        const QString name = bodyOf(request).value(u"model").toString();
        for (const Model& model : models) {
            if (model.name == name)
                return json(QJsonObject{{u"capabilities"_s, QJsonArray::fromStringList(model.capabilities)}});
        }
        return QHttpServerResponse("application/json", R"({"error":"model not found"})",
                                   QHttpServerResponse::StatusCode::NotFound);
    }
    if (request.path == u"/api/generate")
        return QHttpServerResponse("application/x-ndjson", answer.join('\n') + '\n');
    return notFound();
}

QHttpServerResponse FakeOllama::control(const RecordedRequest& request)
{
    if (request.path == u"/fake/setup") {
        const QJsonObject setup = bodyOf(request);
        if (setup.contains(u"models")) {
            models.clear();
            for (const QJsonValue item : setup.value(u"models").toArray()) {
                QStringList capabilities;
                for (const QJsonValue capability : item.toObject().value(u"capabilities").toArray())
                    capabilities.append(capability.toString());
                models.append({item.toObject().value(u"name").toString(), capabilities});
            }
        }
        if (setup.contains(u"answer")) {
            answer.clear();
            for (const QJsonValue line : setup.value(u"answer").toArray())
                answer.append(line.toString().toUtf8());
        }
        return json(QJsonObject{});
    }
    if (request.path == u"/fake/requests") {
        QJsonArray list;
        for (const RecordedRequest& asked : m_http.requests()) {
            if (asked.path.startsWith(u"/fake/"))
                continue;
            const QJsonDocument body = QJsonDocument::fromJson(asked.body);
            list.append(QJsonObject{{u"method"_s, QString::fromLatin1(asked.method)},
                                    {u"path"_s, asked.path},
                                    {u"body"_s, body.isObject() ? QJsonValue(body.object()) : QJsonValue()}});
        }
        return json(QJsonObject{{u"requests"_s, list}});
    }
    return notFound();
}

FakeComfyUi::FakeComfyUi()
{
    m_http.setHandler([this](const RecordedRequest& request) { return respond(request); });
    QHttpServer& server = m_http.server();
    server.addWebSocketUpgradeVerifier(&server, [](const QHttpServerRequest& request) {
        return request.url().path() == u"/ws" ? QHttpServerWebSocketUpgradeResponse::accept()
                                              : QHttpServerWebSocketUpgradeResponse::deny();
    });
    QObject::connect(&server, &QAbstractHttpServer::newWebSocketConnection, &server, [this] { acceptSockets(); });
}

FakeComfyUi::~FakeComfyUi() = default;

QStringList FakeComfyUi::deleted() const
{
    QStringList ids;
    for (const RecordedRequest& request : m_http.requests()) {
        if (request.path == u"/queue") {
            for (const QJsonValue id : bodyOf(request).value(u"delete").toArray())
                ids.append(id.toString());
        }
    }
    return ids;
}

bool FakeComfyUi::wasAsked(const QString& path) const
{
    return std::ranges::any_of(m_http.requests(),
                               [&path](const RecordedRequest& request) { return request.path == path; });
}

void FakeComfyUi::run(const QString& promptId)
{
    const auto event = [&promptId](const QString& type, QJsonObject data) {
        data.insert(u"prompt_id"_s, promptId);
        return QJsonObject{{u"type"_s, type}, {u"data"_s, data}};
    };
    broadcast(event(u"execution_start"_s, {}));
    broadcast(event(u"executing"_s, {{u"node"_s, u"1"_s}}));
    broadcast(event(u"progress"_s, {{u"value"_s, 1}, {u"max"_s, 2}}));
    broadcast(event(u"progress"_s, {{u"value"_s, 2}, {u"max"_s, 2}}));
    broadcast(event(u"executing"_s, {{u"node"_s, u"9"_s}}));
    m_finished.insert(promptId);
    broadcast(event(u"executing"_s, {{u"node"_s, QJsonValue::Null}}));
}

void FakeComfyUi::fail(const QString& promptId, const QString& message)
{
    broadcast(QJsonObject{
        {u"type"_s, u"execution_error"_s},
        {u"data"_s,
         QJsonObject{{u"prompt_id"_s, promptId}, {u"node_type"_s, u"KSampler"_s}, {u"exception_message"_s, message}}}});
}

void FakeComfyUi::interrupt(const QString& promptId)
{
    broadcast(QJsonObject{{u"type"_s, u"execution_interrupted"_s},
                          {u"data"_s, QJsonObject{{u"prompt_id"_s, promptId}, {u"node_type"_s, u"KSampler"_s}}}});
}

void FakeComfyUi::closeSockets()
{
    for (const auto& socket : m_sockets)
        socket->close();
}

QHttpServerResponse FakeComfyUi::control(const RecordedRequest& request)
{
    const QJsonObject body = bodyOf(request);
    const auto names = [&body](const QString& key) {
        QStringList list;
        for (const QJsonValue name : body.value(key).toArray())
            list.append(name.toString());
        return list;
    };
    if (request.path == u"/fake/setup") {
        if (body.contains(u"missingModels"))
            missingModels = names(u"missingModels"_s);
        if (body.contains(u"missingNodes"))
            missingNodes = names(u"missingNodes"_s);
        refusePrompts = body.value(u"refusePrompts").toBool(refusePrompts);
        holdPrompts = body.value(u"holdPrompts").toBool(holdPrompts);
        imageCount = body.value(u"imageCount").toInt(imageCount);
        return json(QJsonObject{});
    }
    if (request.path == u"/fake/prompts") {
        QJsonArray list;
        for (const auto& [id, workflow] : m_prompts)
            list.append(QJsonObject{{u"id"_s, id}, {u"workflow"_s, workflow}});
        return json(QJsonObject{{u"prompts"_s, list}, {u"deleted"_s, QJsonArray::fromStringList(deleted())}});
    }
    if (request.path == u"/fake/run") {
        run(body.value(u"promptId").toString());
        return json(QJsonObject{});
    }
    return notFound();
}

QHttpServerResponse FakeComfyUi::respond(const RecordedRequest& request)
{
    const QString& path = request.path;
    if (path.startsWith(u"/fake/"))
        return control(request);
    if (path == u"/system_stats")
        return json(QJsonObject{{u"system"_s, QJsonObject{{u"comfyui_version"_s, u"0.9.1"_s}}}});
    if (path.startsWith(u"/models/")) {
        QStringList files{u"other.safetensors"_s};
        for (const QString& model : kComfyModels) {
            if (!missingModels.contains(model))
                files.append(model);
        }
        return json(QJsonArray::fromStringList(files));
    }
    if (path.startsWith(u"/object_info/")) {
        const QString node = path.mid(QStringLiteral("/object_info/").size());
        if (missingNodes.contains(node))
            return json(QJsonObject{});
        return json(QJsonObject{{node, QJsonObject{{u"name"_s, node}}}});
    }
    if (path == u"/prompt") {
        if (refusePrompts) {
            const QJsonObject refusal{
                {u"error"_s, QJsonObject{{u"message"_s, u"Prompt outputs failed validation"_s}}},
                {u"node_errors"_s,
                 QJsonObject{{u"6"_s, QJsonObject{{u"errors"_s, QJsonArray{QJsonObject{
                                                                    {u"message"_s, u"Value not in list"_s},
                                                                    {u"details"_s, u"sampler_name: 'euler'"_s}}}}}}}}};
            return QHttpServerResponse("application/json", QJsonDocument(refusal).toJson(QJsonDocument::Compact),
                                       QHttpServerResponse::StatusCode::BadRequest);
        }
        const QString id = u"prompt-%1"_s.arg(m_prompts.size() + 1);
        m_prompts.append({id, bodyOf(request).value(u"prompt"_s).toObject()});
        if (runEarly)
            run(id);
        else if (!holdPrompts)
            QTimer::singleShot(10, &m_http.server(), [this, id] { run(id); });
        return json(QJsonObject{{u"prompt_id"_s, id}, {u"number"_s, m_prompts.size()}});
    }
    if (path == u"/queue")
        return json(QJsonObject{});
    if (path.startsWith(u"/history/")) {
        const QString id = path.mid(QStringLiteral("/history/").size());
        if (!m_finished.contains(id))
            return json(QJsonObject{});
        const auto prompt = std::ranges::find(m_prompts, id, &std::pair<QString, QJsonObject>::first);
        const QString prefix =
            prompt->second.value(u"9").toObject().value(u"inputs").toObject().value(u"filename_prefix").toString();
        QJsonArray saved;
        for (int index = 1; index <= imageCount; ++index) {
            saved.append(QJsonObject{
                {u"filename"_s, u"%1_%2_.png"_s.arg(prefix.section(u'/', -1)).arg(index, 5, 10, QLatin1Char('0'))},
                {u"subfolder"_s, prefix.section(u'/', 0, -2)},
                {u"type"_s, u"output"_s}});
        }
        return json(
            QJsonObject{{id, QJsonObject{{u"outputs"_s, QJsonObject{{u"9"_s, QJsonObject{{u"images"_s, saved}}}}}}}});
    }
    if (path == u"/view")
        return QHttpServerResponse("image/png", image);
    return notFound();
}

void FakeComfyUi::acceptSockets()
{
    QHttpServer& server = m_http.server();
    while (server.hasPendingWebSocketConnections()) {
        std::unique_ptr<QWebSocket> socket = server.nextPendingWebSocketConnection();
        if (!socket)
            continue;
        // ComfyUI greets each socket with its queue's status.
        socket->sendTextMessage(QString::fromUtf8(
            QJsonDocument(QJsonObject{{u"type"_s, u"status"_s}, {u"data"_s, QJsonObject{{u"sid"_s, u"fake"_s}}}})
                .toJson(QJsonDocument::Compact)));
        m_sockets.push_back(std::move(socket));
    }
}

void FakeComfyUi::broadcast(const QJsonObject& event)
{
    const QString text = QString::fromUtf8(QJsonDocument(event).toJson(QJsonDocument::Compact));
    for (const auto& socket : m_sockets) {
        if (socket->state() == QAbstractSocket::ConnectedState)
            socket->sendTextMessage(text);
    }
}

}  // namespace mikserstudio::tests
