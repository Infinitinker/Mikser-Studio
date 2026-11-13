// The ComfyUI backend: checking ComfyUI's models and nodes, queuing the workflow, following it on ComfyUI's
// socket and copying the images it saved.
#include "ComfyUiBackend.h"

#include "ComfyUiWorkflow.h"
#include "HttpRequests.h"
#include "Logging.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QPointer>
#include <QRandomGenerator>
#include <QTimer>
#include <QUuid>
#include <QWebSocket>

#include <memory>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace mikserstudio::services {

namespace {

// Short, so a missing ComfyUI is noticed quickly.
constexpr auto kQuick = 5s;
// ComfyUI checks the whole workflow before it queues it.
constexpr auto kQueue = 30s;
// Saved images can be large; reading one should still not take long on loopback.
constexpr auto kDownload = 120s;
constexpr auto kSocketOpen = 10s;
// The seed's range, as ComfyUI's KSampler takes it and the schema says.
constexpr qint64 kMaxSeed = qint64{1} << 48;

const auto kGenerate = u"image.generate"_s;

struct ComfyInfo {
    QString version;
    QStringList lacking;  // Model files and nodes the workflow needs and ComfyUI does not have.
};

using InspectCallback = std::function<void(std::expected<ComfyInfo, QString>)>;

// Asks ComfyUI for its version and whether it has the workflow's models and nodes, all at once.
void inspect(QNetworkAccessManager& network, const QUrl& baseUrl, const QObject* context, InspectCallback done)
{
    const auto models = comfyui::requiredModels();
    const QStringList nodes = comfyui::requiredNodes();
    struct State {
        ComfyInfo info;
        QString error;
        QStringList lacking;
        int pending = 0;
        InspectCallback done;
    };
    auto state = std::make_shared<State>();
    state->lacking.resize(models.size() + nodes.size());
    state->pending = static_cast<int>(1 + models.size() + nodes.size());
    state->done = std::move(done);
    const auto settle = [state] {
        if (--state->pending > 0)
            return;
        if (!state->error.isEmpty()) {
            state->done(std::unexpected(state->error));
            return;
        }
        for (const QString& name : state->lacking) {
            if (!name.isEmpty())
                state->info.lacking.append(name);
        }
        state->done(state->info);
    };

    sendJsonRequest(
        network, "GET", toolEndpoint(baseUrl, u"/system_stats"_s), {}, kQuick, context,
        [state, settle](std::expected<QJsonDocument, QString> answer) {
            if (answer) {
                state->info.version =
                    answer->object().value(u"system").toObject().value(u"comfyui_version").toString(u"unknown"_s);
            } else {
                state->error = answer.error();
            }
            settle();
        });
    for (qsizetype i = 0; i < models.size(); ++i) {
        const auto& [folder, file] = models.at(i);
        sendJsonRequest(network, "GET", toolEndpoint(baseUrl, u"/models/"_s + folder), {}, kQuick, context,
                        [state, settle, i, file](std::expected<QJsonDocument, QString> answer) {
                            if (!answer)
                                state->error = answer.error();
                            else if (!answer->array().contains(file))
                                state->lacking[i] = file;
                            settle();
                        });
    }
    for (qsizetype i = 0; i < nodes.size(); ++i) {
        const QString& node = nodes.at(i);
        const qsizetype slot = models.size() + i;
        sendJsonRequest(network, "GET", toolEndpoint(baseUrl, u"/object_info/"_s + node), {}, kQuick, context,
                        [state, settle, slot, node](std::expected<QJsonDocument, QString> answer) {
                            if (!answer)
                                state->error = answer.error();
                            else if (!answer->object().contains(node))
                                state->lacking[slot] = u"the node %1"_s.arg(node);
                            settle();
                        });
    }
}

QString notRunning(const QUrl& url)
{
    return u"ComfyUI is not running at %1. Start ComfyUI (install it from https://www.comfy.org if needed) and try again."_s
        .arg(url.toString());
}

QString lacks(const QStringList& lacking)
{
    return u"ComfyUI lacks %1. Add them in ComfyUI, for example from its Qwen Image 2.1 template, and try again."_s.arg(
        lacking.join(u", "_s));
}

// Takes a prompt that has not started off ComfyUI's queue; one that already runs goes on.
void deleteQueued(QNetworkAccessManager& network, const QUrl& baseUrl, const QString& promptId)
{
    sendRequest(network, "POST", toolEndpoint(baseUrl, u"/queue"_s),
                QJsonDocument(QJsonObject{{u"delete"_s, QJsonArray{promptId}}}), kQuick, &network,
                [promptId](const std::expected<QByteArray, QString>& answer) {
                    if (!answer)
                        qCWarning(lcAi).noquote()
                            << "Could not take" << promptId << "off ComfyUI's queue:" << answer.error();
                });
}

QUrl socketUrl(const QUrl& baseUrl, const QString& clientId)
{
    QUrl url = toolEndpoint(baseUrl, u"/ws"_s, QUrlQuery{{u"clientId"_s, clientId}});
    url.setScheme(baseUrl.scheme() == u"https" ? u"wss"_s : u"ws"_s);
    return url;
}

// image.generate: checks ComfyUI, opens its socket, queues the workflow, follows it and copies the images.
class ImageTask final : public AiTask {
public:
    ImageTask(QNetworkAccessManager& network, QUrl baseUrl, const QJsonObject& params, AiTaskContext context)
        : m_network(network)
        , m_baseUrl(std::move(baseUrl))
        , m_context(std::move(context))
        , m_clientId(QUuid::createUuid().toString(QUuid::WithoutBraces))
    {
        m_request = comfyui::ImageRequest{
            .prompt = params.value(u"prompt").toString(),
            .negativePrompt = params.value(u"negative_prompt").toString(),
            .width = params.value(u"width").toInt(1024),
            .height = params.value(u"height").toInt(1024),
            .steps = params.value(u"steps").toInt(25),
            .seed = params.contains(u"seed")
                        ? params.value(u"seed").toInteger()
                        : static_cast<qint64>(QRandomGenerator::global()->bounded(static_cast<quint64>(kMaxSeed))),
        };
        m_openTimer.setSingleShot(true);
        m_openTimer.setInterval(kSocketOpen);
        connect(&m_openTimer, &QTimer::timeout, this, [this] {
            fail(u"ComfyUI's socket did not open at %1. Check that ComfyUI is still running and try again."_s.arg(
                m_baseUrl.toString()));
        });
        QTimer::singleShot(0, this, &ImageTask::begin);
    }

    void cancel() override
    {
        m_canceled = true;
        if (m_reply)
            m_reply->abort();
        if (m_socket)
            m_socket->abort();
        if (!m_promptId.isEmpty() && !m_finishedInComfy)
            deleteQueued(m_network, m_baseUrl, m_promptId);
        if (m_queueReply) {
            // The prompt is on its way: take it off the queue as soon as ComfyUI names it.
            QNetworkAccessManager* network = &m_network;
            disconnect(m_queueReply, nullptr, this, nullptr);
            connect(m_queueReply, &QNetworkReply::finished, network,
                    [network, baseUrl = m_baseUrl, reply = m_queueReply] {
                        const QString id =
                            QJsonDocument::fromJson(reply->readAll()).object().value(u"prompt_id").toString();
                        if (!id.isEmpty())
                            deleteQueued(*network, baseUrl, id);
                    });
        }
    }

private:
    void begin()
    {
        inspect(m_network, m_baseUrl, this, [this](std::expected<ComfyInfo, QString> info) {
            if (m_canceled)
                return;
            if (!info) {
                fail(u"ComfyUI is not reachable at %1 (%2). Start ComfyUI and try again."_s.arg(m_baseUrl.toString(),
                                                                                                info.error()));
                return;
            }
            if (!info->lacking.isEmpty()) {
                fail(lacks(info->lacking));
                return;
            }
            m_version = info->version;
            openSocket();
        });
    }

    void openSocket()
    {
        m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
        m_socket->setProxy(QNetworkProxy::NoProxy);
        connect(m_socket, &QWebSocket::textMessageReceived, this, &ImageTask::onMessage);
        connect(m_socket, &QWebSocket::disconnected, this, [this] {
            if (!m_canceled && !m_finished)
                fail(u"ComfyUI closed the connection before the image was done. Check that it is still running."_s);
        });
        m_openTimer.start();
        m_socket->open(socketUrl(m_baseUrl, m_clientId));
    }

    void onMessage(const QString& text)
    {
        if (m_canceled || m_finished)
            return;
        if (!m_greeted) {
            // ComfyUI greets each socket with its queue's status: the socket is open.
            m_greeted = true;
            m_openTimer.stop();
            queuePrompt();
            return;
        }
        const QJsonObject event = QJsonDocument::fromJson(text.toUtf8()).object();
        if (m_promptId.isEmpty())
            m_early.append(event);  // Sent before ComfyUI's answer to the prompt arrived.
        else
            follow(event);
    }

    void queuePrompt()
    {
        const QString prefix = u"%1/%2"_s.arg(comfyui::kOutputFolder, m_context.jobId);
        const QJsonObject body{{u"prompt"_s, comfyui::imageWorkflow(m_request, prefix)}, {u"client_id"_s, m_clientId}};
        m_queueReply = sendJsonRequest(
            m_network, "POST", toolEndpoint(m_baseUrl, u"/prompt"_s), QJsonDocument(body), kQueue, this,
            [this](std::expected<QJsonDocument, QString> answer) {
                m_queueReply = nullptr;
                if (m_canceled)
                    return;
                if (!answer) {
                    fail(u"ComfyUI did not take the job (%1). Start ComfyUI and try again."_s.arg(answer.error()));
                    return;
                }
                m_promptId = answer->object().value(u"prompt_id").toString();
                if (m_promptId.isEmpty()) {
                    fail(u"ComfyUI took the job but gave it no id. Check that ComfyUI is up to date and try again."_s);
                    return;
                }
                m_context.report(0.0, u"Waiting in ComfyUI's queue"_s);
                const QList<QJsonObject> early = std::exchange(m_early, {});
                for (const QJsonObject& event : early) {
                    if (m_finished || m_finishedInComfy)
                        return;
                    follow(event);
                }
            });
    }

    // One event of ComfyUI's socket about our prompt.
    void follow(const QJsonObject& event)
    {
        const QJsonObject data = event.value(u"data").toObject();
        if (data.value(u"prompt_id").toString() != m_promptId || m_finishedInComfy)
            return;
        const QString type = event.value(u"type").toString();
        if (type == u"progress" && data.value(u"max").toInt() > 0) {
            const int value = data.value(u"value").toInt();
            const int total = data.value(u"max").toInt();
            // Progress never goes back when a stage after the sampling starts.
            m_progress = std::max(m_progress, std::min(static_cast<double>(value) / total, 1.0) * 0.95);
            m_context.report(m_progress, u"Step %1 of %2"_s.arg(value).arg(total));
        } else if ((type == u"executing" && data.value(u"node").isNull()) || type == u"execution_success") {
            m_finishedInComfy = true;
            readHistory();
        } else if (type == u"executing") {
            if (const QString stage = comfyui::stage(data.value(u"node").toVariant().toString()); !stage.isEmpty())
                m_context.report(m_progress, stage);
        } else if (type == u"execution_error") {
            m_finishedInComfy = true;
            QString message = data.value(u"exception_message").toString().trimmed();
            if (message.isEmpty())
                message = u"an error"_s;
            fail(u"ComfyUI stopped with %1 in %2."_s.arg(message, data.value(u"node_type").toString(u"a node"_s)));
        } else if (type == u"execution_interrupted") {
            m_finishedInComfy = true;
            fail(u"The job was stopped in ComfyUI."_s);
        }
    }

    void readHistory()
    {
        m_reply = sendJsonRequest(
            m_network, "GET", toolEndpoint(m_baseUrl, u"/history/"_s + m_promptId), {}, kQuick, this,
            [this](std::expected<QJsonDocument, QString> answer) {
                if (m_canceled)
                    return;
                if (!answer) {
                    fail(
                        u"ComfyUI did not list the image it made (%1). Check that ComfyUI is still running and try again."_s
                            .arg(answer.error()));
                    return;
                }
                const QJsonArray images = answer->object()
                                              .value(m_promptId)
                                              .toObject()
                                              .value(u"outputs")
                                              .toObject()
                                              .value(comfyui::kSaveNode.toString())
                                              .toObject()
                                              .value(u"images")
                                              .toArray();
                if (images.isEmpty()) {
                    fail(u"ComfyUI finished without saving an image. Look at ComfyUI's log for why."_s);
                    return;
                }
                for (const QJsonValue image : images)
                    m_images.append(image.toObject());
                download(0);
            });
    }

    // Copies the images ComfyUI saved into the job's folder, one after the other.
    void download(qsizetype index)
    {
        if (index >= m_images.size()) {
            succeed();
            return;
        }
        const QJsonObject image = m_images.at(index);
        const QString filename = image.value(u"filename").toString();
        const QUrlQuery query{
            {u"filename"_s, filename},
            {u"subfolder"_s, image.value(u"subfolder").toString()},
            {u"type"_s, image.value(u"type").toString(u"output"_s)},
        };
        QString suffix = QFileInfo(filename).suffix();
        if (suffix.isEmpty())
            suffix = u"png"_s;
        const QString target =
            QDir(m_context.outputDirectory).filePath(u"image-%1.%2"_s.arg(index + 1).arg(suffix.toLower()));
        m_reply = sendRequest(
            m_network, "GET", toolEndpoint(m_baseUrl, u"/view"_s, query), {}, kDownload, this,
            [this, index, filename, target](std::expected<QByteArray, QString> answer) {
                if (m_canceled)
                    return;
                if (!answer) {
                    fail(u"ComfyUI could not send %1 (%2). Check that ComfyUI is still running and try again."_s.arg(
                        filename, answer.error()));
                    return;
                }
                QFile file(target);
                if (!file.open(QIODevice::WriteOnly) || file.write(*answer) != answer->size()) {
                    fail(u"The image could not be saved to “%1”: %2. Check that there is free space."_s.arg(
                        QDir::toNativeSeparators(target), file.errorString()));
                    return;
                }
                m_outputs.append(AiOutput{target, u"image"_s});
                download(index + 1);
            });
    }

    void succeed()
    {
        m_finished = true;
        if (m_socket)
            m_socket->close();
        m_context.report(1.0, u"Done"_s);
        m_context.finish(AiOutcome{
            .outputs = m_outputs,
            .provenance =
                QJsonObject{
                    {u"tools"_s, QJsonObject{{u"comfyui"_s, m_version}}},
                    {u"model"_s, QJsonObject{{u"name"_s, comfyui::kDiffusionModel.toString()},
                                             {u"textEncoder"_s, comfyui::kTextEncoder.toString()},
                                             {u"vae"_s, comfyui::kVae.toString()}}},
                    {u"method"_s, QJsonObject{{u"workflow"_s, u"Qwen Image 2.1 text to image"_s},
                                              {u"sampler"_s, u"euler"_s},
                                              {u"scheduler"_s, u"simple"_s},
                                              {u"cfg"_s, 1.0},
                                              {u"steps"_s, m_request.steps},
                                              {u"seed"_s, m_request.seed},
                                              {u"width"_s, comfyui::modelSize(m_request.width)},
                                              {u"height"_s, comfyui::modelSize(m_request.height)},
                                              {u"promptId"_s, m_promptId}}},
                },
            .data = QJsonObject{{u"seed"_s, m_request.seed}},
        });
    }

    void fail(const QString& message)
    {
        if (m_finished)
            return;
        m_finished = true;
        m_openTimer.stop();
        if (m_socket)
            m_socket->abort();
        m_context.finish(std::unexpected(message));
    }

    QNetworkAccessManager& m_network;
    QUrl m_baseUrl;
    AiTaskContext m_context;
    QString m_clientId;
    comfyui::ImageRequest m_request;
    QString m_version;
    QTimer m_openTimer;
    QPointer<QWebSocket> m_socket;
    QPointer<QNetworkReply> m_reply;
    QPointer<QNetworkReply> m_queueReply;
    QString m_promptId;
    QList<QJsonObject> m_early;
    QList<QJsonObject> m_images;
    QList<AiOutput> m_outputs;
    double m_progress = 0.0;
    bool m_greeted = false;
    bool m_finishedInComfy = false;  // ComfyUI is done with the prompt: nothing to take off its queue.
    bool m_finished = false;  // finish has run.
    bool m_canceled = false;
};

}  // namespace

ComfyUiBackend::ComfyUiBackend(QUrl baseUrl, QObject* parent)
    : AiBackend(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_baseUrl(std::move(baseUrl))
{
    m_network->setProxy(QNetworkProxy::NoProxy);
}

QString ComfyUiBackend::id() const
{
    return u"comfyui"_s;
}

QString ComfyUiBackend::title() const
{
    return u"ComfyUI"_s;
}

QList<AiOperation> ComfyUiBackend::operations() const
{
    const auto size = [](const QString& description) {
        return QJsonObject{{u"type"_s, u"integer"_s},
                           {u"minimum"_s, 256},
                           {u"maximum"_s, 2048},
                           {u"default"_s, 1024},
                           {u"description"_s, description}};
    };
    return {AiOperation{
        .id = kGenerate,
        .title = u"Generate image"_s,
        .description = u"Makes a still image from a prompt with Qwen Image 2.1 in ComfyUI."_s,
        .paramsSchema =
            QJsonObject{
                {u"type"_s, u"object"_s},
                {u"properties"_s,
                 QJsonObject{
                     {u"prompt"_s, QJsonObject{{u"type"_s, u"string"_s},
                                               {u"minLength"_s, 1},
                                               {u"description"_s, u"What the image shows."_s}}},
                     {u"negative_prompt"_s, QJsonObject{{u"type"_s, u"string"_s},
                                                        {u"default"_s, u""_s},
                                                        {u"description"_s, u"What the image should not show."_s}}},
                     {u"width"_s, size(u"In pixels, rounded down to a multiple of 16."_s)},
                     {u"height"_s, size(u"In pixels, rounded down to a multiple of 16."_s)},
                     {u"steps"_s, QJsonObject{{u"type"_s, u"integer"_s},
                                              {u"minimum"_s, 1},
                                              {u"maximum"_s, 100},
                                              {u"default"_s, 25},
                                              {u"description"_s, u"Sampling steps."_s}}},
                     {u"seed"_s,
                      QJsonObject{{u"type"_s, u"integer"_s},
                                  {u"minimum"_s, 0},
                                  {u"maximum"_s, static_cast<double>(kMaxSeed)},
                                  {u"description"_s,
                                   u"The noise seed; a new one each time when omitted. The provenance keeps it."_s}}},
                 }},
                {u"required"_s, QJsonArray{u"prompt"_s}},
                {u"additionalProperties"_s, false},
            },
    }};
}

void ComfyUiBackend::checkAvailability(const QObject* context, std::function<void(AiAvailability)> done)
{
    inspect(*m_network, m_baseUrl, context,
            [baseUrl = m_baseUrl, done = std::move(done)](std::expected<ComfyInfo, QString> info) {
                if (!info)
                    done({false, notRunning(baseUrl)});
                else if (!info->lacking.isEmpty())
                    done({false, lacks(info->lacking)});
                else
                    done({true, {}});
            });
}

std::unique_ptr<AiTask> ComfyUiBackend::start(const QString& op, const QJsonObject& params, AiTaskContext context)
{
    Q_ASSERT(op == kGenerate);
    return std::make_unique<ImageTask>(*m_network, m_baseUrl, params, std::move(context));
}

}  // namespace mikserstudio::services
