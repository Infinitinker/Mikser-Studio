// End to end: ai.run through the built mikser-studio, run headless as the E2E instance and driven through its
// local API, against a fake ComfyUI in the test process. The image comes into the project with its
// provenance, without a real AI tool, and /v1/events reports the job.
#include "FakeAiTools.h"
#include "HttpTestSupport.h"
#include "ServicesTestSupport.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QWebSocket>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using mikserstudio::tests::FakeComfyUi;
using mikserstudio::tests::HttpReply;
using mikserstudio::tests::postCommand;
using mikserstudio::tests::waitFor;

namespace {

// The result of a command run through the local API; empty when it failed (the reason goes to INFO).
// The fake ComfyUI answers from this thread's event loop while the request waits.
QJsonObject runCommand(const QJsonObject& discovery, const QString& id, const QJsonObject& params = {})
{
    const HttpReply reply = postCommand(discovery.value(u"baseUrl"_s).toString(),
                                        discovery.value(u"token"_s).toString(), id, params);
    if (reply.status != 200) {
        UNSCOPED_INFO(id.toStdString() << " answered " << reply.status << ": " << reply.body.toStdString());
        return {};
    }
    return reply.object().value(u"result"_s).toObject();
}

}  // namespace

TEST_CASE("ai.run makes an image in a fake ComfyUI and brings it into the project", "[e2e][ai]")
{
    FakeComfyUi comfy;
    QTemporaryDir folder;
    QProcess studio;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(u"MIKSERSTUDIO_INSTANCE"_s, u"e2e"_s);
    environment.insert(u"MIKSERSTUDIO_CONFIG_DIR"_s, folder.filePath(u"config"_s));
    environment.insert(u"MIKSERSTUDIO_DATA_DIR"_s, folder.filePath(u"data"_s));
    environment.insert(u"MIKSERSTUDIO_CACHE_DIR"_s, folder.filePath(u"cache"_s));
    environment.insert(u"QT_FORCE_STDERR_LOGGING"_s, u"1"_s);
    studio.setProcessEnvironment(environment);
    studio.setProcessChannelMode(QProcess::MergedChannels);
    studio.start(QStringLiteral(MIKSERSTUDIO_APP), {u"--headless"_s});

    QJsonObject discovery;
    const bool started = waitFor(
        [&] {
            QFile file(folder.filePath(u"data/api.json"_s));
            if (file.open(QIODevice::ReadOnly))
                discovery = QJsonDocument::fromJson(file.readAll()).object();
            return studio.state() == QProcess::NotRunning ||
                   discovery.value(u"pid"_s).toInteger() == studio.processId();
        },
        60s);
    INFO(QString::fromUtf8(studio.readAll()).toStdString());
    REQUIRE(started);
    REQUIRE(studio.state() == QProcess::Running);

    QWebSocket events;
    QList<QJsonObject> received;
    QObject::connect(&events, &QWebSocket::textMessageReceived, [&received](const QString& message) {
        received.append(QJsonDocument::fromJson(message.toUtf8()).object());
    });
    events.open(
        QUrl(discovery.value(u"eventsUrl"_s).toString() + u"?token="_s + discovery.value(u"token"_s).toString()));
    REQUIRE(waitFor([&events] { return events.state() == QAbstractSocket::ConnectedState; }, 10s));

    const QJsonObject settings = runCommand(discovery, u"ai.set_settings"_s,
                                            {{u"comfyuiUrl"_s, comfy.url().toString()}});
    REQUIRE(settings.value(u"comfyuiUrl"_s).toString() == comfy.url().toString());

    const QString jobId = runCommand(discovery, u"ai.run"_s,
                                     {{u"op"_s, u"image.generate"_s},
                                      {u"params"_s, QJsonObject{{u"prompt"_s, u"A lighthouse at dusk"_s},
                                                                {u"width"_s, 512},
                                                                {u"height"_s, 512},
                                                                {u"steps"_s, 2}}}})
                              .value(u"jobId"_s)
                              .toString();
    REQUIRE_FALSE(jobId.isEmpty());

    QJsonObject job;
    const bool finished = waitFor(
        [&] {
            for (const QJsonValue value : runCommand(discovery, u"ai.jobs"_s).value(u"jobs"_s).toArray()) {
                if (value.toObject().value(u"id"_s).toString() == jobId)
                    job = value.toObject();
            }
            const QString state = job.value(u"state"_s).toString();
            return state == u"succeeded" || state == u"failed" || state == u"canceled";
        },
        60s);
    INFO(QJsonDocument(job).toJson().toStdString());
    REQUIRE(finished);
    REQUIRE(job.value(u"state"_s).toString() == u"succeeded"_s);
    REQUIRE(comfy.prompts().size() == 1);

    // The job's events, from the core job manager.
    const auto eventsOfJob = [&received, &jobId] {
        QStringList types;
        for (const QJsonObject& event : received) {
            if (event.value(u"jobId"_s).toString() == jobId)
                types.append(event.value(u"type"_s).toString());
        }
        return types;
    };
    REQUIRE(waitFor([&] { return eventsOfJob().contains(u"job.finished"_s); }, 10s));
    const QStringList types = eventsOfJob();
    INFO(types.join(u", "_s).toStdString());
    CHECK(types.constFirst() == u"job.created"_s);
    CHECK(types.contains(u"job.progress"_s));
    CHECK(types.constLast() == u"job.finished"_s);

    const QString mediaId = job.value(u"placement"_s).toObject().value(u"mediaIds"_s).toArray().first().toString();
    const QJsonArray media = runCommand(discovery, u"project.list"_s).value(u"media"_s).toArray();
    const auto item = std::ranges::find_if(
        media, [&mediaId](const QJsonValue& value) { return value.toObject().value(u"id"_s).toString() == mediaId; });
    REQUIRE(item != media.end());
    const QJsonObject provenance = item->toObject().value(u"provenance"_s).toObject();
    CHECK(provenance.value(u"jobId"_s).toString() == jobId);
    CHECK(provenance.value(u"backend"_s).toString() == u"comfyui"_s);
    CHECK(provenance.value(u"params"_s).toObject().value(u"prompt"_s).toString() == u"A lighthouse at dusk"_s);
    CHECK(item->toObject().value(u"path"_s).toString().startsWith(
        QDir::fromNativeSeparators(folder.filePath(u"cache"_s))));

    studio.kill();
    studio.waitForFinished();
}
