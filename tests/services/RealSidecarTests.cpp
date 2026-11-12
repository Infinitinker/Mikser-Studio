// Starts the real Python sidecar from ai/.venv, the way the app does. Skipped when that environment
// is not set up (docs/development.md) or MIKSERSTUDIO_SKIP_REAL_SIDECAR is set; the upscale test
// also needs ffmpeg on PATH.

#include "ServicesTestSupport.h"
#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/ProjectCommands.h"
#include "mikserstudio/core/UndoStack.h"
#include "mikserstudio/services/AiClient.h"
#include "mikserstudio/services/AiCommands.h"
#include "mikserstudio/services/SidecarManager.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <optional>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using namespace mikserstudio::services;
using mikserstudio::tests::portIsClosed;
using mikserstudio::tests::waitFor;

namespace {

void skipWithoutEnvironment()
{
    if (qEnvironmentVariableIsSet("MIKSERSTUDIO_SKIP_REAL_SIDECAR"))
        SKIP("MIKSERSTUDIO_SKIP_REAL_SIDECAR is set");
    const QDir venv(QDir(QStringLiteral(MIKSERSTUDIO_AI_PROJECT_DIR)).filePath(u".venv"_s));
#ifdef Q_OS_WIN
    const bool installed = QFileInfo(venv.filePath(u"Scripts/mikserstudio-ai.exe"_s)).isFile();
#else
    const bool installed = QFileInfo(venv.filePath(u"bin/mikserstudio-ai"_s)).isFile();
#endif
    if (!installed)
        SKIP("ai/.venv is not set up");
}

SidecarOptions realOptions(const QTemporaryDir& dir)
{
    SidecarOptions options;
    options.aiDirectory = QStringLiteral(MIKSERSTUDIO_AI_PROJECT_DIR);
    options.cacheDirectory = dir.filePath(u"cache"_s);
    options.maxRestarts = 0;
    return options;
}

}  // namespace

TEST_CASE("The real sidecar starts from its environment and answers", "[services][real-sidecar]")
{
    skipWithoutEnvironment();
    QTemporaryDir dir;
    SidecarManager sidecar(realOptions(dir));
    sidecar.start();
    REQUIRE(waitFor([&] { return sidecar.state() != SidecarState::Starting; }, 130s));
    INFO(sidecar.errorString().toStdString());
    REQUIRE(sidecar.state() == SidecarState::Running);

    AiClient client;
    client.setEndpoint(sidecar.baseUrl(), sidecar.token());
    std::optional<std::expected<QJsonObject, QString>> health;
    client.health(&client, [&](auto answer) { health = std::move(answer); });
    REQUIRE(waitFor([&] { return health.has_value(); }));
    REQUIRE(*health);
    CHECK(health->value().value(u"status").toString() == u"ok"_s);
    CHECK(health->value().value(u"service").toString() == u"mikserstudio-ai"_s);

    std::optional<std::expected<QList<AiBackendInfo>, QString>> backends;
    client.backends(&client, [&](auto answer) { backends = std::move(answer); });
    REQUIRE(waitFor([&] { return backends.has_value(); }, 30s));
    INFO((*backends ? QString() : backends->error()).toStdString());
    REQUIRE(*backends);
    QStringList ids;
    for (const AiBackendInfo& backend : backends->value())
        ids.append(backend.id);
    CHECK(ids.contains(u"builtin"_s));
    CHECK(ids.contains(u"ollama"_s));

    const int port = sidecar.baseUrl().port();
    sidecar.stop();
    CHECK(portIsClosed(port));
}

TEST_CASE("ai.upscale_clip runs on the real sidecar and lands on the timeline", "[services][real-sidecar]")
{
    skipWithoutEnvironment();
    const QString ffmpeg = QStandardPaths::findExecutable(u"ffmpeg"_s);
    if (ffmpeg.isEmpty())
        SKIP("ffmpeg is not on PATH");

    QTemporaryDir dir;
    const QString clipPath = dir.filePath(u"tiny.mp4"_s);
    QProcess encode;
    encode.start(ffmpeg, {u"-hide_banner"_s, u"-loglevel"_s, u"error"_s, u"-y"_s, u"-f"_s, u"lavfi"_s, u"-i"_s,
                          u"testsrc=size=160x90:rate=30:duration=1"_s, u"-f"_s, u"lavfi"_s, u"-i"_s,
                          u"sine=frequency=440:duration=1"_s, u"-c:v"_s, u"libx264"_s, u"-pix_fmt"_s, u"yuv420p"_s,
                          u"-c:a"_s, u"aac"_s, u"-shortest"_s, clipPath});
    REQUIRE(encode.waitForFinished(60000));
    REQUIRE(encode.exitCode() == 0);

    mikserstudio::core::UndoStack undo;
    mikserstudio::core::CommandBus bus(undo);
    mikserstudio::core::ProjectModel project;
    mikserstudio::tests::FakeProbe probe;
    mikserstudio::core::registerProjectCommands(bus, project, probe);
    SidecarManager sidecar(realOptions(dir));
    AiClient client;
    registerAiCommands(bus, project, sidecar, client);

    const QString mediaId = bus.execute(u"project.import_media"_s, {{u"path"_s, clipPath}})->value(u"mediaId").toString();
    const QString clipId = bus.execute(u"timeline.add_clip"_s, {{u"mediaId"_s, mediaId}, {u"trackId"_s, u"V1"_s},
                                                                {u"position"_s, 12}, {u"in"_s, 0}, {u"out"_s, 30}})
                               ->value(u"clipId")
                               .toString();
    const auto submitted = bus.execute(u"ai.upscale_clip"_s, {{u"clipId"_s, clipId}, {u"scale"_s, 2}});
    REQUIRE(submitted);
    const QString jobId = submitted->value(u"jobId").toString();

    QJsonObject job;
    REQUIRE(waitFor(
        [&] {
            job = bus.execute(u"ai.jobs"_s)->value(u"jobs").toArray().at(0).toObject();
            const QString state = job.value(u"state").toString();
            return state == u"succeeded" || state == u"failed" || state == u"canceled";
        },
        300s));
    INFO(QJsonDocument(job).toJson().toStdString());
    REQUIRE(job.value(u"state").toString() == u"succeeded"_s);
    CHECK(job.value(u"id").toString() == jobId);

    const QJsonObject placement = job.value(u"placement").toObject();
    CHECK(placement.value(u"trackId").toString() == u"V2"_s);
    const mikserstudio::core::MediaItem* upscaled = project.findMedia(placement.value(u"mediaId").toString());
    REQUIRE(upscaled);
    CHECK(upscaled->path.endsWith(u"/tiny_x2.mkv"_s));
    CHECK(QFileInfo(upscaled->path).size() > 0);
    CHECK(upscaled->provenance.value(u"backend").toString() == u"builtin"_s);
    CHECK(upscaled->provenance.value(u"method").toObject().value(u"ai").toBool(true) == false);
    CHECK_FALSE(upscaled->provenance.value(u"tools").toObject().value(u"ffmpeg").toString().isEmpty());
    CHECK(job.value(u"result").toObject().value(u"width").toInt() == 320);
    const mikserstudio::core::Clip* placed = project.findClip(placement.value(u"clipId").toString());
    REQUIRE(placed);
    CHECK(placed->trackId == u"V2"_s);
    CHECK(placed->position == 12);
}
