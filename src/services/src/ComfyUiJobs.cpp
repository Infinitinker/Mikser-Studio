// Running comfyui.run_prompt jobs: ComfyUI's socket, queuing the workflow, following its events,
// copying the files it saved, and taking only the job's own prompt off the queue when canceled.
#include "mikserstudio/services/ComfyUiJobs.h"

#include "HttpRequests.h"
#include "Logging.h"
#include "NetworkAccess.h"
#include "mikserstudio/core/JobManager.h"
#include "mikserstudio/core/JsonSchema.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>
#include <QWebSocket>

#include <algorithm>
#include <chrono>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace mikserstudio::services {

namespace {

const auto kKind = u"comfyui.run_prompt"_s;

constexpr auto kQuick = 5s;
// ComfyUI checks the whole workflow before it queues it.
constexpr auto kQueue = 30s;
// Saved files can be large; reading one should still not take long.
constexpr auto kDownload = 120s;
constexpr auto kSocketOpen = 10s;
// A queued prompt is given up when ComfyUI's socket stays silent this long: it sends progress while
// it works and status while the prompt waits, so a silent ComfyUI is stuck.
constexpr auto kSilence = 10min;
// How long closing the app waits for the requests that take its prompts off ComfyUI's queue.
constexpr auto kShutdownWait = 3s;

QJsonObject inputSchema()
{
    const auto text = [](int minLength) { return QJsonObject{{u"type"_s, u"string"_s}, {u"minLength"_s, minLength}}; };
    return QJsonObject{
        {u"type"_s, u"object"_s},
        {u"properties"_s,
         QJsonObject{
             {u"url"_s, text(1)},
             {u"workflow"_s, QJsonObject{{u"type"_s, u"object"_s}, {u"minProperties"_s, 1}}},
             {u"outputNode"_s, text(1)},
             {u"stages"_s, QJsonObject{{u"type"_s, u"object"_s},
                                       {u"additionalProperties"_s, QJsonObject{{u"type"_s, u"string"_s}}}}},
             {u"title"_s, text(0)},
         }},
        {u"required"_s, QJsonArray{u"url"_s, u"workflow"_s, u"outputNode"_s}},
        {u"additionalProperties"_s, false},
    };
}

QString shown(const QUrl& url)
{
    return url.toString(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment);
}

QUrl socketUrl(const QUrl& baseUrl, const QString& clientId)
{
    QUrl url = toolEndpoint(baseUrl, u"/ws"_s, QUrlQuery{{u"clientId"_s, clientId}});
    url.setScheme(baseUrl.scheme() == u"https" ? u"wss"_s : u"ws"_s);
    return url;
}

// What a file ComfyUI saved is, from its suffix.
QString kindOf(const QString& suffix)
{
    static const QStringList video{u"mp4"_s, u"webm"_s, u"mov"_s, u"mkv"_s, u"avi"_s};
    static const QStringList audio{u"wav"_s, u"flac"_s, u"mp3"_s, u"ogg"_s, u"opus"_s, u"m4a"_s};
    if (video.contains(suffix))
        return u"video"_s;
    if (audio.contains(suffix))
        return u"audio"_s;
    return u"image"_s;
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

}  // namespace

// One job: open the socket, queue the workflow once ComfyUI greets it, follow the prompt, then copy
// its files. Ends the job once, through m_jobs, unless the job was canceled.
class ComfyUiJobs::Run final : public QObject {
public:
    struct Input {
        QUrl url;
        QJsonObject workflow;
        QString outputNode;
        QJsonObject stages;
    };

    // `ended` runs once the job has ended on its own, not when it is stopped.
    Run(core::JobManager& jobs, QNetworkAccessManager& network, QString jobId, Input input, QString folder,
        std::function<void()> ended)
        : m_jobs(jobs)
        , m_network(network)
        , m_jobId(std::move(jobId))
        , m_input(std::move(input))
        , m_folder(std::move(folder))
        , m_ended(std::move(ended))
        , m_clientId(QUuid::createUuid().toString(QUuid::WithoutBraces))
    {
        m_openTimer.setSingleShot(true);
        m_openTimer.setInterval(kSocketOpen);
        connect(&m_openTimer, &QTimer::timeout, this, [this] {
            fail(u"ComfyUI’s socket did not open at %1. Check that ComfyUI is running and try again."_s.arg(
                shown(m_input.url)));
        });
        m_silenceTimer.setSingleShot(true);
        m_silenceTimer.setInterval(kSilence);
        connect(&m_silenceTimer, &QTimer::timeout, this, [this] {
            if (!m_finishedInComfy)
                deleteQueued(m_network, m_input.url, m_promptId);
            fail(u"ComfyUI sent nothing about the job for %1 minutes. Check ComfyUI’s window or log, then try again."_s
                     .arg(std::chrono::duration_cast<std::chrono::minutes>(kSilence).count()));
        });
    }

    void start()
    {
        if (!QDir().mkpath(m_folder)) {
            fail(u"Mikser Studio could not make the folder “%1” for ComfyUI’s files. Check that there is free space."_s
                     .arg(QDir::toNativeSeparators(m_folder)));
            return;
        }
        m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
        m_socket->setProxy(QNetworkProxy::NoProxy);
        connect(m_socket, &QWebSocket::textMessageReceived, this, &Run::onMessage);
        // A socket that never opened may report its error before or after it says it disconnected.
        const auto lost = [this] {
            if (m_done)
                return;
            if (!m_greeted) {
                fail(u"ComfyUI is not reachable at %1 (%2). Start ComfyUI and try again."_s.arg(
                    shown(m_input.url), m_socket->errorString()));
            } else {
                fail(u"ComfyUI closed the connection before the job was done. Check that it is still running."_s);
            }
        };
        connect(m_socket, &QWebSocket::errorOccurred, this, lost);
        connect(m_socket, &QWebSocket::disconnected, this, lost);
        m_openTimer.start();
        m_socket->open(socketUrl(m_input.url, m_clientId));
    }

    // The job was canceled: stop, and take its prompt off ComfyUI's queue unless ComfyUI is done with it.
    void stop()
    {
        if (m_done)
            return;
        end();
        if (m_reply)
            m_reply->abort();
        if (m_socket)
            m_socket->abort();
        if (!m_promptId.isEmpty() && !m_finishedInComfy)
            deleteQueued(m_network, m_input.url, m_promptId);
        if (m_queueReply) {
            // The prompt is on its way: take it off the queue as soon as ComfyUI names it.
            QNetworkAccessManager* network = &m_network;
            disconnect(m_queueReply, nullptr, this, nullptr);
            connect(m_queueReply, &QNetworkReply::finished, network,
                    [network, url = m_input.url, reply = m_queueReply] {
                        const QString id =
                            QJsonDocument::fromJson(reply->readAll()).object().value(u"prompt_id").toString();
                        if (!id.isEmpty())
                            deleteQueued(*network, url, id);
                    });
        }
    }

    // The app is closing: as stop(), and the job fails with why.
    void close()
    {
        if (m_done)
            return;
        stop();
        m_jobs.fail(m_jobId, u"The ComfyUI job stopped: Mikser Studio is closing."_s);
    }

private:
    void onMessage(const QString& text)
    {
        if (m_done)
            return;
        if (!m_greeted) {
            // ComfyUI greets each socket with its queue's status: the socket is open.
            m_greeted = true;
            m_openTimer.stop();
            queuePrompt();
            return;
        }
        const QJsonObject event = QJsonDocument::fromJson(text.toUtf8()).object();
        if (m_silenceTimer.isActive())
            m_silenceTimer.start();
        if (m_promptId.isEmpty())
            m_early.append(event);  // Sent before ComfyUI's answer to the prompt arrived.
        else
            follow(event);
    }

    void queuePrompt()
    {
        QNetworkRequest request(toolEndpoint(m_input.url, u"/prompt"_s));
        request.setTransferTimeout(kQueue);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
        const QJsonObject body{{u"prompt"_s, m_input.workflow}, {u"client_id"_s, m_clientId}};
        m_queueReply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
        QNetworkReply* reply = m_queueReply;
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            m_queueReply = nullptr;
            if (m_done)
                return;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray answer = reply->readAll();
            if (status == 0) {
                fail(u"ComfyUI did not take the job at %1 (%2). Start ComfyUI and try again."_s.arg(
                    shown(m_input.url), reply->errorString()));
                return;
            }
            if (status < 200 || status > 299) {
                refused(status, answer);
                return;
            }
            m_promptId = QJsonDocument::fromJson(answer).object().value(u"prompt_id").toString();
            if (m_promptId.isEmpty()) {
                fail(u"ComfyUI took the job but gave it no id. Check that ComfyUI is up to date and try again."_s);
                return;
            }
            m_jobs.progress(m_jobId, 0.0, u"Waiting in ComfyUI’s queue"_s);
            m_silenceTimer.start();
            const QList<QJsonObject> early = std::exchange(m_early, {});
            for (const QJsonObject& event : early) {
                if (m_done || m_finishedInComfy)
                    return;
                follow(event);
            }
        });
    }

    // One event of ComfyUI's socket about the job's prompt.
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
            m_jobs.progress(m_jobId, m_progress, u"Step %1 of %2"_s.arg(value).arg(total));
        } else if ((type == u"executing" && data.value(u"node").isNull()) || type == u"execution_success") {
            m_finishedInComfy = true;
            readHistory();
        } else if (type == u"executing") {
            const QString stage = m_input.stages.value(data.value(u"node").toVariant().toString()).toString();
            if (!stage.isEmpty())
                m_jobs.progress(m_jobId, m_progress, stage);
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
            m_network, "GET", toolEndpoint(m_input.url, u"/history/"_s + m_promptId), {}, kQuick, this,
            [this](std::expected<QJsonDocument, QString> answer) {
                if (m_done)
                    return;
                if (!answer) {
                    fail(
                        u"ComfyUI did not list the files it made (%1). Check that ComfyUI is still running and try again."_s
                            .arg(answer.error()));
                    return;
                }
                const QJsonObject outputs =
                    answer->object().value(m_promptId).toObject().value(u"outputs"_s).toObject();
                const QJsonObject saved = outputs.value(m_input.outputNode).toObject();
                for (const QString& key : {u"images"_s, u"gifs"_s, u"audio"_s}) {
                    for (const QJsonValue file : saved.value(key).toArray())
                        m_files.append(file.toObject());
                }
                if (m_files.isEmpty()) {
                    fail(u"ComfyUI finished without saving a file. Look at ComfyUI’s log for why."_s);
                    return;
                }
                download(0);
            });
    }

    // Copies the files ComfyUI saved into the job's folder, one after the other.
    void download(qsizetype index)
    {
        if (index >= m_files.size()) {
            succeed();
            return;
        }
        const QJsonObject file = m_files.at(index);
        const QString filename = file.value(u"filename").toString();
        const QUrlQuery query{
            {u"filename"_s, filename},
            {u"subfolder"_s, file.value(u"subfolder").toString()},
            {u"type"_s, file.value(u"type").toString(u"output"_s)},
        };
        QString suffix = QFileInfo(filename).suffix().toLower();
        if (suffix.isEmpty())
            suffix = u"png"_s;
        const QString kind = kindOf(suffix);
        const QString target = QDir(m_folder).filePath(u"%1-%2.%3"_s.arg(kind).arg(index + 1).arg(suffix));
        m_reply = sendRequest(
            m_network, "GET", toolEndpoint(m_input.url, u"/view"_s, query), {}, kDownload, this,
            [this, index, filename, target, kind](std::expected<QByteArray, QString> answer) {
                if (m_done)
                    return;
                if (!answer) {
                    fail(u"ComfyUI could not send %1 (%2). Check that ComfyUI is still running and try again."_s.arg(
                        filename, answer.error()));
                    return;
                }
                // Closed before the next step: the last download ends the job, whose files are then read.
                QFile out(target);
                const bool saved = out.open(QIODevice::WriteOnly) && out.write(*answer) == answer->size() &&
                                   out.flush();
                out.close();
                if (!saved) {
                    fail(u"The file could not be saved to “%1”: %2. Check that there is free space."_s.arg(
                        QDir::toNativeSeparators(target), out.errorString()));
                    return;
                }
                m_outputs.append(QJsonObject{{u"path"_s, target}, {u"kind"_s, kind}});
                download(index + 1);
            });
    }

    void refused(int status, const QByteArray& body)
    {
        QJsonObject refusal{{u"status"_s, status}, {u"body"_s, QString::fromUtf8(body)}};
        QJsonParseError error;
        if (const QJsonDocument document = QJsonDocument::fromJson(body, &error);
            error.error == QJsonParseError::NoError && document.isObject()) {
            refusal.insert(u"json"_s, document.object());
        }
        end();
        m_jobs.succeed(m_jobId, QJsonObject{{u"refused"_s, refusal}});
        m_ended();
    }

    void succeed()
    {
        end();
        if (m_socket)
            m_socket->close();
        m_jobs.progress(m_jobId, 1.0, u"Done"_s);
        m_jobs.succeed(m_jobId, QJsonObject{
                                    {u"promptId"_s, m_promptId},
                                    {u"outputs"_s, m_outputs},
                                    {u"provenance"_s,
                                     QJsonObject{{u"promptId"_s, m_promptId}, {u"comfyui"_s, shown(m_input.url)}}},
                                });
        m_ended();
    }

    void fail(const QString& message)
    {
        if (m_done)
            return;
        end();
        if (m_socket)
            m_socket->abort();
        m_jobs.fail(m_jobId, message);
        m_ended();
    }

    void end()
    {
        m_done = true;
        m_openTimer.stop();
        m_silenceTimer.stop();
        if (m_socket)
            m_socket->disconnect(this);
    }

    core::JobManager& m_jobs;
    QNetworkAccessManager& m_network;
    QString m_jobId;
    Input m_input;
    QString m_folder;
    std::function<void()> m_ended;
    QString m_clientId;
    QTimer m_openTimer;
    QTimer m_silenceTimer;  // Runs while the prompt is queued or running in ComfyUI.
    QPointer<QWebSocket> m_socket;
    QPointer<QNetworkReply> m_reply;
    QPointer<QNetworkReply> m_queueReply;
    QString m_promptId;
    QList<QJsonObject> m_early;
    QList<QJsonObject> m_files;
    QJsonArray m_outputs;
    double m_progress = 0.0;
    bool m_greeted = false;
    bool m_finishedInComfy = false;  // ComfyUI is done with the prompt: nothing to take off its queue.
    bool m_done = false;  // The job has ended, or was stopped.
};

ComfyUiJobs::ComfyUiJobs(core::JobManager& jobs, ComfyUiJobOptions options, QObject* parent)
    : QObject(parent)
    , m_jobs(jobs)
    , m_options(std::move(options))
    , m_network(new QNetworkAccessManager(this))
{
    m_network->setProxy(QNetworkProxy::NoProxy);
    m_jobs.registerKind(
        kKind,
        core::JobKind{
            .permission = u"network"_s,
            .start = [this](core::JobManager& manager, core::JobSpec spec,
                            const QJsonObject& input) -> std::expected<QString, core::CommandError> {
                if (const QStringList problems = core::JsonSchema::validate(input, inputSchema());
                    !problems.isEmpty()) {
                    return std::unexpected(
                        core::CommandError{core::CommandErrorCode::InvalidParams,
                                           u"The %1 input doesn’t fit: %2"_s.arg(kKind, problems.join(u"; "_s))});
                }
                Run::Input call{
                    .url = QUrl(input.value(u"url").toString().trimmed(), QUrl::StrictMode),
                    .workflow = input.value(u"workflow").toObject(),
                    .outputNode = input.value(u"outputNode").toString(),
                    .stages = input.value(u"stages").toObject(),
                };
                if (!isWebAddress(call.url)) {
                    return std::unexpected(core::CommandError{
                        core::CommandErrorCode::InvalidParams,
                        u"“%1” is not an http address. Enter one with a host and port, such as http://127.0.0.1:8188."_s
                            .arg(input.value(u"url").toString())});
                }
                const auto allowed = checkNetworkAccess(spec.network, call.url,
                                                        m_options.apiPort ? m_options.apiPort() : std::nullopt);
                if (!allowed)
                    return std::unexpected(
                        core::CommandError{core::CommandErrorCode::PermissionDenied, allowed.error()});
                spec.title = input.value(u"title").toString(u"ComfyUI: run a workflow"_s);
                auto shared = std::make_shared<Run::Input>(std::move(call));
                return manager.submit(
                    std::move(spec),
                    [this, shared](const QString& id) {
                        auto run = std::make_unique<Run>(m_jobs, *m_network, id, *shared,
                                                         QDir(m_options.outputDirectory).filePath(id),
                                                         [this, id] { forget(id); });
                        Run* started = run.get();
                        m_runs.emplace(id, std::move(run));
                        started->start();
                    },
                    [this](const QString& id) {
                        if (const auto it = m_runs.find(id); it != m_runs.end())
                            it->second->stop();
                        forget(id);
                    });
            },
        });
}

void ComfyUiJobs::forget(const QString& id)
{
    // Later: this runs inside the run's own handlers.
    if (const auto it = m_runs.find(id); it != m_runs.end()) {
        it->second.release()->deleteLater();
        m_runs.erase(it);
    }
}

ComfyUiJobs::~ComfyUiJobs()
{
    m_jobs.unregisterKind(kKind);
    const auto runs = std::exchange(m_runs, {});
    for (const auto& [id, run] : runs)
        run->close();
    // The requests that take the prompts off ComfyUI's queue go with the network manager, so they are
    // given a moment to go out.
    const auto pending = [this] {
        const QList<QNetworkReply*> replies = m_network->findChildren<QNetworkReply*>(Qt::FindDirectChildrenOnly);
        return std::ranges::any_of(replies, [](const QNetworkReply* reply) { return reply->isRunning(); });
    };
    if (!QCoreApplication::instance() || !pending())
        return;
    QEventLoop loop;
    QTimer poll;
    const QDeadlineTimer deadline(kShutdownWait);
    connect(&poll, &QTimer::timeout, &loop, [&] {
        if (deadline.hasExpired() || !pending())
            loop.quit();
    });
    poll.start(20ms);
    loop.exec(QEventLoop::ExcludeUserInputEvents);
}

}  // namespace mikserstudio::services
