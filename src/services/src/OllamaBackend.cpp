// The Ollama backend: checking that Ollama runs and has models, choosing a model and streaming its answer.
#include "OllamaBackend.h"

#include "HttpRequests.h"
#include "Logging.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>

#include <memory>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace mikserstudio::services {

namespace {

// Short, so a missing Ollama is noticed quickly.
constexpr auto kQuick = 3s;
// /api/show reads the model file's metadata, which can take a moment on a cold disk.
constexpr auto kShow = 15s;
// Loading a large model before the first token can take minutes: the limit is on silence, not on
// the whole answer.
constexpr auto kGenerateSilence = 600s;

const auto kGenerate = u"text.generate"_s;
const auto kThinkEnd = u"</think>"_s;
const auto kNoModels = u"Ollama has no models. Download one with \"ollama pull <model>\" and try again."_s;

struct OllamaModel {
    QString name;
    QString digest;
    QJsonObject details;
};

QList<OllamaModel> parseModels(const QJsonDocument& document)
{
    QList<OllamaModel> models;
    for (const QJsonValue item : document.object().value(u"models").toArray()) {
        const QJsonObject model = item.toObject();
        if (const QString name = model.value(u"name").toString(); !name.isEmpty())
            models.append({name, model.value(u"digest").toString(), model.value(u"details").toObject()});
    }
    return models;
}

QJsonObject modelProvenance(const OllamaModel& model)
{
    QJsonObject provenance{{u"name"_s, model.name}, {u"digest"_s, model.digest}};
    const std::pair<QString, QString> keys[] = {
        {u"family"_s, u"family"_s},
        {u"parameter_size"_s, u"parameterSize"_s},
        {u"quantization_level"_s, u"quantization"_s},
    };
    for (const auto& [key, name] : keys) {
        if (const QString value = model.details.value(key).toVariant().toString(); !value.isEmpty())
            provenance.insert(name, value);
    }
    return provenance;
}

QString notRunning(const QUrl& url)
{
    return u"Ollama is not running at %1. Start Ollama (install it from https://ollama.com if needed) and try again."_s
        .arg(url.toString());
}

// text.generate: finds the model, then streams the answer.
class GenerateTask final : public AiTask {
public:
    GenerateTask(QNetworkAccessManager& network, QUrl baseUrl, const QJsonObject& params, AiTaskContext context)
        : m_network(network)
        , m_baseUrl(std::move(baseUrl))
        , m_prompt(params.value(u"prompt").toString())
        , m_wanted(params.value(u"model").toString())
        , m_context(std::move(context))
    {
        QTimer::singleShot(0, this, &GenerateTask::begin);
    }

    void cancel() override
    {
        m_canceled = true;
        if (m_reply)
            m_reply->abort();
    }

private:
    void begin()
    {
        m_reply = sendJsonRequest(m_network, "GET", toolEndpoint(m_baseUrl, u"/api/version"_s), {}, kQuick, this,
                                  [this](std::expected<QJsonDocument, QString> answer) {
                                      if (m_canceled)
                                          return;
                                      if (!answer) {
                                          unreachable(answer.error());
                                          return;
                                      }
                                      m_version = answer->object().value(u"version").toString(u"unknown"_s);
                                      listModels();
                                  });
    }

    void listModels()
    {
        m_reply = sendJsonRequest(m_network, "GET", toolEndpoint(m_baseUrl, u"/api/tags"_s), {}, kQuick, this,
                                  [this](std::expected<QJsonDocument, QString> answer) {
                                      if (m_canceled)
                                          return;
                                      if (!answer) {
                                          unreachable(answer.error());
                                          return;
                                      }
                                      m_models = parseModels(*answer);
                                      chooseModel();
                                  });
    }

    void chooseModel()
    {
        if (m_models.isEmpty()) {
            fail(kNoModels);
            return;
        }
        if (m_wanted.isEmpty()) {
            checkCandidate(0);
            return;
        }
        for (qsizetype i = 0; i < m_models.size(); ++i) {
            const QString& name = m_models.at(i).name;
            if (name == m_wanted || name == m_wanted + u":latest"_s) {
                m_onlyCandidate = true;
                checkCandidate(i);
                return;
            }
        }
        QStringList installed;
        for (const OllamaModel& model : std::as_const(m_models))
            installed.append(model.name);
        fail(
            u"The model “%1” is not installed in Ollama (installed: %2). Download it with \"ollama pull %1\" or choose an installed one."_s
                .arg(m_wanted, installed.join(u", "_s)));
    }

    // Asks Ollama what models[index] can do; the first that can generate text is used.
    void checkCandidate(qsizetype index)
    {
        if (index >= m_models.size()) {
            fail(
                u"None of the installed Ollama models can generate text. Download one with \"ollama pull <model>\" (for example a Llama, Qwen or Gemma model) and try again."_s);
            return;
        }
        const OllamaModel model = m_models.at(index);
        m_reply = sendJsonRequest(
            m_network, "POST", toolEndpoint(m_baseUrl, u"/api/show"_s),
            QJsonDocument(QJsonObject{{u"model"_s, model.name}}), kShow, this,
            [this, index, model](std::expected<QJsonDocument, QString> answer) {
                if (m_canceled)
                    return;
                if (!answer) {
                    fail(u"Ollama did not describe the model %1 (%2). Start Ollama and try again."_s.arg(
                        model.name, answer.error()));
                    return;
                }
                const QJsonValue capabilities = answer->object().value(u"capabilities");
                // An Ollama that does not say what a model can do is trusted to generate text.
                if (!capabilities.isArray() || capabilities.toArray().contains(u"completion"_s)) {
                    generate(model);
                    return;
                }
                if (!m_onlyCandidate) {
                    checkCandidate(index + 1);
                    return;
                }
                QStringList offers;
                for (const QJsonValue capability : capabilities.toArray())
                    offers.append(capability.toString());
                fail(u"The Ollama model “%1” cannot generate text (it offers: %2). Choose another model."_s.arg(
                    model.name, offers.isEmpty() ? u"nothing"_s : offers.join(u", "_s)));
            });
    }

    void generate(const OllamaModel& model)
    {
        m_model = model;
        m_context.report(0.0, u"Loading %1"_s.arg(model.name));
        QNetworkRequest request(toolEndpoint(m_baseUrl, u"/api/generate"_s));
        request.setTransferTimeout(kGenerateSilence);
        request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
        const QJsonObject body{{u"model"_s, model.name}, {u"prompt"_s, m_prompt}, {u"stream"_s, true}};
        QNetworkReply* reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
        m_reply = reply;
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
        connect(reply, &QNetworkReply::readyRead, this, [this, reply] { readLines(*reply); });
        connect(reply, &QNetworkReply::finished, this, [this, reply] { answered(*reply); });
    }

    void readLines(QNetworkReply& reply)
    {
        if (m_canceled || !m_streamError.isEmpty())
            return;
        const int status = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200)
            return;  // The whole error answer is read when it has finished.
        m_buffer += reply.readAll();
        qsizetype end = 0;
        while ((end = m_buffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_buffer.left(end).trimmed();
            m_buffer.remove(0, end + 1);
            if (!line.isEmpty())
                readChunk(line);
        }
    }

    void readChunk(const QByteArray& line)
    {
        const QJsonObject chunk = QJsonDocument::fromJson(line).object();
        if (chunk.contains(u"error")) {
            m_streamError = chunk.value(u"error").toVariant().toString();
            return;
        }
        const QString response = chunk.value(u"response").toString();
        m_answer += response;
        ++m_chunks;
        const bool thinking = !chunk.value(u"thinking").toString().isEmpty() && response.isEmpty();
        m_context.report(
            0.0, u"%1 with %2 (%3 tokens)"_s.arg(thinking ? u"Thinking"_s : u"Writing"_s, m_model.name).arg(m_chunks));
    }

    void answered(QNetworkReply& reply)
    {
        if (m_canceled)
            return;
        const int status = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString problem = m_streamError;
        if (status != 0 && status != 200)
            problem = errorFromAnswer(status, reply.readAll());
        else if (problem.isEmpty() && reply.error() != QNetworkReply::NoError)
            problem = reply.errorString();
        if (problem.isEmpty()) {
            readLines(reply);
            if (const QByteArray rest = m_buffer.trimmed(); !rest.isEmpty())
                readChunk(rest);
            problem = m_streamError;
        }
        if (!problem.isEmpty()) {
            qCWarning(lcAi).noquote() << "Ollama failed while answering with" << m_model.name << "-" << problem;
            fail(
                u"Ollama stopped answering with %1. Check that Ollama is still running and that the model works in Ollama itself, then try again; Mikser Studio’s log has Ollama’s message."_s
                    .arg(m_model.name));
            return;
        }
        const QString text = stripReasoning(m_answer);
        m_context.finish(AiOutcome{
            .outputs = {},
            .provenance =
                QJsonObject{
                    {u"tools"_s, QJsonObject{{u"ollama"_s, m_version}}},
                    {u"model"_s, modelProvenance(m_model)},
                },
            .data = QJsonObject{{u"text"_s, text}, {u"model"_s, m_model.name}},
        });
    }

    void unreachable(const QString& reason)
    {
        fail(u"Ollama is not reachable at %1 (%2). Start Ollama and try again."_s.arg(m_baseUrl.toString(), reason));
    }

    void fail(const QString& message) const { m_context.finish(std::unexpected(message)); }

    QNetworkAccessManager& m_network;
    QUrl m_baseUrl;
    QString m_prompt;
    QString m_wanted;
    AiTaskContext m_context;
    QPointer<QNetworkReply> m_reply;
    bool m_canceled = false;
    bool m_onlyCandidate = false;
    QString m_version;
    QList<OllamaModel> m_models;
    OllamaModel m_model;
    QByteArray m_buffer;
    QString m_answer;
    QString m_streamError;
    int m_chunks = 0;
};

}  // namespace

OllamaBackend::OllamaBackend(QUrl baseUrl, QObject* parent)
    : AiBackend(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_baseUrl(std::move(baseUrl))
{
    m_network->setProxy(QNetworkProxy::NoProxy);
}

QString OllamaBackend::id() const
{
    return u"ollama"_s;
}

QString OllamaBackend::title() const
{
    return u"Ollama"_s;
}

QList<AiOperation> OllamaBackend::operations() const
{
    return {AiOperation{
        .id = kGenerate,
        .title = u"Generate text"_s,
        .description = u"Answers a prompt with a local language model served by Ollama."_s,
        .paramsSchema =
            QJsonObject{
                {u"type"_s, u"object"_s},
                {u"properties"_s,
                 QJsonObject{
                     {u"prompt"_s, QJsonObject{{u"type"_s, u"string"_s},
                                               {u"minLength"_s, 1},
                                               {u"description"_s, u"What to ask the model."_s}}},
                     {u"model"_s,
                      QJsonObject{{u"type"_s, u"string"_s},
                                  {u"minLength"_s, 1},
                                  {u"description"_s, u"Ollama model name; the first installed model when omitted."_s}}},
                 }},
                {u"required"_s, QJsonArray{u"prompt"_s}},
                {u"additionalProperties"_s, false},
            },
    }};
}

void OllamaBackend::checkAvailability(const QObject* context, std::function<void(AiAvailability)> done)
{
    const QUrl baseUrl = m_baseUrl;
    sendJsonRequest(*m_network, "GET", toolEndpoint(baseUrl, u"/api/version"_s), {}, kQuick, context,
                    [this, context, baseUrl, done = std::move(done)](std::expected<QJsonDocument, QString> version) {
                        if (!version) {
                            done({false, notRunning(baseUrl)});
                            return;
                        }
                        sendJsonRequest(*m_network, "GET", toolEndpoint(baseUrl, u"/api/tags"_s), {}, kQuick, context,
                                        [baseUrl, done](std::expected<QJsonDocument, QString> tags) {
                                            if (!tags)
                                                done({false, notRunning(baseUrl)});
                                            else if (parseModels(*tags).isEmpty())
                                                done({false, kNoModels});
                                            else
                                                done({true, {}});
                                        });
                    });
}

std::unique_ptr<AiTask> OllamaBackend::start(const QString& op, const QJsonObject& params, AiTaskContext context)
{
    Q_ASSERT(op == kGenerate);
    return std::make_unique<GenerateTask>(*m_network, m_baseUrl, params, std::move(context));
}

QString stripReasoning(const QString& text)
{
    const qsizetype end = text.lastIndexOf(kThinkEnd);
    return (end < 0 ? text : text.mid(end + kThinkEnd.size())).trimmed();
}

}  // namespace mikserstudio::services
