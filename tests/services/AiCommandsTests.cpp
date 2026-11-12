// The ai.* commands against a fake service: backends, upscaling that places the result on the timeline with
// provenance and undo, text generation, failed and canceled jobs, and a sidecar crash.
#include "ServicesTestSupport.h"
#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/core/ProjectCommands.h"
#include "mikserstudio/core/UndoStack.h"
#include "mikserstudio/services/AiClient.h"
#include "mikserstudio/services/AiCommands.h"
#include "mikserstudio/services/SidecarManager.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

#include <memory>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using namespace mikserstudio;
using namespace mikserstudio::services;
using mikserstudio::tests::FakeProbe;
using mikserstudio::tests::waitFor;

namespace {

struct AiFixture {
    QTemporaryDir dir;
    core::UndoStack undo;
    core::CommandBus bus{undo};
    core::ProjectModel project;
    FakeProbe probe;
    std::unique_ptr<SidecarManager> sidecar;
    std::unique_ptr<AiClient> client;

    explicit AiFixture(const QStringList& sidecarArguments = {}, bool environmentMissing = false)
    {
        registerProjectCommands(bus, project, probe);
        SidecarOptions options;
        options.cacheDirectory = dir.filePath(u"cache"_s);
        options.healthInterval = 60s;
        options.restartDelay = 50ms;
        options.maxRestarts = 1;
        if (environmentMissing) {
            // The ai folder without its .venv.
            QFile pyproject(dir.filePath(u"pyproject.toml"_s));
            REQUIRE(pyproject.open(QIODevice::WriteOnly));
            pyproject.close();
            options.aiDirectory = dir.path();
        } else {
            options.program = QStringLiteral(MIKSERSTUDIO_FAKE_SIDECAR);
            options.arguments = sidecarArguments;
        }
        sidecar = std::make_unique<SidecarManager>(options);
        client = std::make_unique<AiClient>();
        registerAiCommands(bus, project, *sidecar, *client);
    }

    QJsonObject run(const QString& id, const QJsonObject& params = {})
    {
        auto result = bus.execute(id, params);
        if (!result)
            FAIL(id.toStdString() << " failed: " << result.error().message.toStdString());
        return *result;
    }

    QString writeFile(const QString& name) const
    {
        const QString path = dir.filePath(name);
        QFile file(path);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("media");
        return path;
    }

    QString addClip(const QString& file, const QString& trackId, qint64 position, qint64 in = 10, qint64 out = 70)
    {
        const QString mediaId =
            run(u"project.import_media"_s, {{u"path"_s, writeFile(file)}}).value(u"mediaId").toString();
        return run(u"timeline.add_clip"_s, {{u"mediaId"_s, mediaId},
                                            {u"trackId"_s, trackId},
                                            {u"position"_s, position},
                                            {u"in"_s, in},
                                            {u"out"_s, out}})
            .value(u"clipId")
            .toString();
    }

    QJsonObject job(const QString& id)
    {
        for (const QJsonValue value : run(u"ai.jobs"_s).value(u"jobs").toArray()) {
            if (value.toObject().value(u"id").toString() == id)
                return value.toObject();
        }
        FAIL("No job " << id.toStdString());
        return {};
    }

    QJsonObject waitFinished(const QString& id)
    {
        REQUIRE(waitFor([&] {
            const QString state = job(id).value(u"state").toString();
            return state == u"succeeded" || state == u"failed" || state == u"canceled";
        }));
        return job(id);
    }
};

}  // namespace

TEST_CASE("The AI commands are registered in the AI category", "[services][commands]")
{
    AiFixture fixture;
    for (const QString& id : {u"ai.status"_s, u"ai.backends"_s, u"ai.jobs"_s, u"ai.cancel_job"_s, u"ai.upscale_clip"_s,
                              u"ai.generate_text"_s}) {
        const core::CommandSpec* spec = fixture.bus.spec(id);
        REQUIRE(spec);
        CHECK(spec->category == u"AI"_s);
        CHECK_FALSE(spec->undoable);
        // Only the reads are read-only; ai.backends starts the service on demand.
        CHECK(spec->readOnly == (id == u"ai.status"_s || id == u"ai.jobs"_s));
    }
    const QJsonObject status = fixture.run(u"ai.status"_s);
    CHECK(status.value(u"state").toString() == u"stopped"_s);
    CHECK(status.value(u"activeJobs").toInt() == 0);
    CHECK(fixture.run(u"ai.jobs"_s).value(u"jobs").toArray().isEmpty());
}

TEST_CASE("The commands go away with the client", "[services][commands]")
{
    AiFixture fixture;
    fixture.client.reset();
    CHECK_FALSE(fixture.bus.contains(u"ai.status"_s));
    CHECK_FALSE(fixture.bus.contains(u"ai.upscale_clip"_s));
}

TEST_CASE("ai.backends starts the service on demand and then lists the backends", "[services][commands]")
{
    AiFixture fixture;
    auto first = fixture.bus.execute(u"ai.backends"_s);
    REQUIRE_FALSE(first);
    CHECK(first.error().code == core::CommandErrorCode::NotAvailable);
    CHECK(first.error().message == u"The AI service is starting. Try again in a moment."_s);
    CHECK(fixture.sidecar->state() == SidecarState::Starting);

    REQUIRE(waitFor([&] { return fixture.bus.execute(u"ai.backends"_s).has_value(); }));
    const QJsonArray backends = fixture.run(u"ai.backends"_s).value(u"backends").toArray();
    REQUIRE(backends.size() == 1);
    CHECK(backends[0].toObject().value(u"id").toString() == u"fake"_s);
    CHECK(backends[0].toObject().value(u"ops").toArray().size() == 2);
    const QJsonObject status = fixture.run(u"ai.status"_s);
    CHECK(status.value(u"state").toString() == u"running"_s);
    CHECK(status.value(u"baseUrl").toString().startsWith(u"http://127.0.0.1:"_s));
}

TEST_CASE("ai.upscale_clip places the result above the clip, with provenance and undo", "[services][commands]")
{
    AiFixture fixture;
    const QString sourceClip = fixture.addClip(u"clip.mp4"_s, u"V1"_s, 30);
    const QString sourceMedia = fixture.project.findClip(sourceClip)->mediaId;
    const QJsonObject submitted = fixture.run(u"ai.upscale_clip"_s, {{u"clipId"_s, sourceClip}, {u"scale"_s, 3}});
    const QString jobId = submitted.value(u"jobId").toString();
    CHECK(jobId.startsWith(u"aijob-"_s));
    CHECK(fixture.job(jobId).value(u"state").toString() == u"queued"_s);

    const QJsonObject job = fixture.waitFinished(jobId);
    INFO(QJsonDocument(job).toJson().toStdString());
    REQUIRE(job.value(u"state").toString() == u"succeeded"_s);
    CHECK(job.value(u"progress").toDouble() == 1.0);
    CHECK(job.value(u"op").toString() == u"video.upscale"_s);
    CHECK(job.value(u"title").toString() == u"Upscale \"clip.mp4\" x3"_s);
    CHECK(job.value(u"params").toObject().value(u"scale").toInt() == 3);
    CHECK(job.value(u"remoteId").toString().startsWith(u"job-"_s));

    const QJsonObject placement = job.value(u"placement").toObject();
    CHECK(placement.value(u"trackId").toString() == u"V2"_s);
    const QString mediaId = placement.value(u"mediaId").toString();
    const QString clipId = placement.value(u"clipId").toString();

    const core::MediaItem* media = fixture.project.findMedia(mediaId);
    REQUIRE(media);
    CHECK(media->name == u"clip.mp4 (upscaled x3)"_s);
    CHECK(media->path.endsWith(u"clip_x3.mkv"_s));
    CHECK(media->provenance.value(u"backend").toString() == u"fake"_s);
    CHECK(media->provenance.value(u"op").toString() == u"video.upscale"_s);
    CHECK(media->provenance.value(u"sourceClipId").toString() == sourceClip);
    CHECK(media->provenance.value(u"sourceMediaId").toString() == sourceMedia);
    CHECK(fixture.project.media().last().id == mediaId);

    const core::Clip* clip = fixture.project.findClip(clipId);
    REQUIRE(clip);
    CHECK(clip->trackId == u"V2"_s);
    CHECK(clip->position == 30);
    CHECK(clip->in == 10);
    CHECK(clip->out == 70);

    // One undo step removes the clip and the imported media; the source clip stays.
    CHECK(fixture.undo.undoText() == u"Place AI Result"_s);
    fixture.run(u"edit.undo"_s);
    CHECK_FALSE(fixture.project.findClip(clipId));
    CHECK_FALSE(fixture.project.findMedia(mediaId));
    CHECK(fixture.project.findClip(sourceClip));
    CHECK(fixture.undo.undoText() == u"Add Clip"_s);  // The fixture's own placement of the source.

    fixture.run(u"edit.redo"_s);
    REQUIRE(fixture.project.findClip(clipId));
    CHECK(fixture.project.findMedia(mediaId)->provenance.value(u"sourceClipId").toString() == sourceClip);
}

TEST_CASE("ai.upscale_clip on the top video track adds a track for the result", "[services][commands]")
{
    AiFixture fixture;
    const QString sourceClip = fixture.addClip(u"top.mp4"_s, u"V2"_s, 5);
    const QString jobId = fixture.run(u"ai.upscale_clip"_s, {{u"clipId"_s, sourceClip}}).value(u"jobId").toString();
    const QJsonObject job = fixture.waitFinished(jobId);
    INFO(QJsonDocument(job).toJson().toStdString());
    REQUIRE(job.value(u"state").toString() == u"succeeded"_s);
    CHECK(job.value(u"placement").toObject().value(u"trackId").toString() == u"V3"_s);

    const core::Track* added = fixture.project.findTrack(u"V3"_s);
    REQUIRE(added);
    CHECK(added->kind == core::TrackKind::Video);
    const core::Clip* clip = fixture.project.findClip(job.value(u"placement").toObject().value(u"clipId").toString());
    REQUIRE(clip);
    CHECK(clip->trackId == u"V3"_s);
    CHECK(clip->position == 5);

    // One undo step removes the clip, the new track and the media.
    CHECK(fixture.undo.undoText() == u"Place AI Result"_s);
    fixture.run(u"edit.undo"_s);
    CHECK_FALSE(fixture.project.findTrack(u"V3"_s));
    CHECK(fixture.project.media().size() == 1);
    CHECK(fixture.project.clips().size() == 1);
    fixture.run(u"edit.redo"_s);
    CHECK(fixture.project.findTrack(u"V3"_s));
    CHECK(fixture.project.clips().size() == 2);
}

TEST_CASE("An upscale that finishes after the project was replaced leaves the new project alone",
          "[services][commands]")
{
    AiFixture fixture;
    const QString sourceClip = fixture.addClip(u"clip.mp4"_s, u"V1"_s, 0);
    const QString jobId = fixture.run(u"ai.upscale_clip"_s, {{u"clipId"_s, sourceClip}}).value(u"jobId").toString();

    // Reopening the same project keeps the clip ids, so only the session tells the projects apart.
    const QJsonObject saved = fixture.project.toJson();
    REQUIRE(fixture.project.loadJson(saved));
    fixture.undo.clear();

    const QJsonObject job = fixture.waitFinished(jobId);
    CHECK(job.value(u"state").toString() == u"failed"_s);
    CHECK(
        job.value(u"error").toString() ==
        u"The project was replaced before the upscale finished, so the result was not added to it. Open the original project and run the upscale again."_s);
    CHECK(fixture.project.toJson() == saved);
    CHECK_FALSE(fixture.undo.canUndo());

    // Jobs started in the new project are placed as usual.
    const QString next = fixture.run(u"ai.upscale_clip"_s, {{u"clipId"_s, sourceClip}}).value(u"jobId").toString();
    CHECK(fixture.waitFinished(next).value(u"state").toString() == u"succeeded"_s);
}

TEST_CASE("ai.upscale_clip refuses clips it cannot upscale", "[services][commands]")
{
    AiFixture fixture;
    const QString top = fixture.addClip(u"top.mp4"_s, u"V2"_s, 0);
    const QString still = fixture.addClip(u"still.png"_s, u"V1"_s, 0, 0, 30);

    auto image = fixture.bus.execute(u"ai.upscale_clip"_s, {{u"clipId"_s, still}});
    REQUIRE_FALSE(image);
    CHECK(image.error().message == u"\"still.png\" is not a video, so it cannot be upscaled. Choose a video clip."_s);

    auto missing = fixture.bus.execute(u"ai.upscale_clip"_s, {{u"clipId"_s, u"clip-nope"_s}});
    REQUIRE_FALSE(missing);
    CHECK(missing.error().message.startsWith(u"There is no clip clip-nope"_s));

    auto badScale = fixture.bus.execute(u"ai.upscale_clip"_s, {{u"clipId"_s, top}, {u"scale"_s, 5}});
    REQUIRE_FALSE(badScale);
    CHECK(badScale.error().code == core::CommandErrorCode::InvalidParams);
    CHECK(fixture.run(u"ai.jobs"_s).value(u"jobs").toArray().isEmpty());
}

TEST_CASE("An upscale whose target is taken is imported but reported as not placed", "[services][commands]")
{
    AiFixture fixture;
    const QString source = fixture.addClip(u"source.mp4"_s, u"V1"_s, 0);
    const QString jobId = fixture.run(u"ai.upscale_clip"_s, {{u"clipId"_s, source}}).value(u"jobId").toString();
    fixture.addClip(u"blocker.mp4"_s, u"V2"_s, 20);

    const QJsonObject job = fixture.waitFinished(jobId);
    CHECK(job.value(u"state").toString() == u"failed"_s);
    CHECK(job.value(u"error").toString().startsWith(
        u"The upscaled video was imported into the media bin, but it was not placed on V2: "_s));
    CHECK(fixture.project.media().last().name == u"source.mp4 (upscaled x2)"_s);
    CHECK(fixture.project.clips().size() == 2);

    CHECK(fixture.undo.undoText() == u"Place AI Result"_s);
    fixture.run(u"edit.undo"_s);
    CHECK(fixture.project.media().size() == 2);
}

TEST_CASE("A failed sidecar job fails the AI job with its message", "[services][commands]")
{
    AiFixture fixture;
    const QString clip = fixture.addClip(u"will-fail.mp4"_s, u"V1"_s, 0);
    const QString jobId = fixture.run(u"ai.upscale_clip"_s, {{u"clipId"_s, clip}}).value(u"jobId").toString();
    const QJsonObject job = fixture.waitFinished(jobId);
    CHECK(job.value(u"state").toString() == u"failed"_s);
    CHECK(job.value(u"error").toString() == u"fake upscale failure"_s);
    CHECK(fixture.project.media().size() == 1);
}

TEST_CASE("ai.generate_text returns the answer in the job result", "[services][commands]")
{
    AiFixture fixture;
    const QString jobId = fixture.run(u"ai.generate_text"_s, {{u"prompt"_s, u"hi"_s}, {u"model"_s, u"tiny"_s}})
                              .value(u"jobId")
                              .toString();
    const QJsonObject job = fixture.waitFinished(jobId);
    REQUIRE(job.value(u"state").toString() == u"succeeded"_s);
    CHECK(job.value(u"result").toObject().value(u"text").toString() == u"echo: hi"_s);
    CHECK(job.value(u"params").toObject() == QJsonObject{{u"prompt"_s, u"hi"_s}, {u"model"_s, u"tiny"_s}});
    CHECK(job.value(u"title").toString() == u"Generate text: hi"_s);
    CHECK(job.value(u"placement").toObject().isEmpty());
}

TEST_CASE("A job the sidecar refuses fails with the sidecar's reason", "[services][commands]")
{
    AiFixture fixture;
    const QString jobId =
        fixture.run(u"ai.generate_text"_s, {{u"prompt"_s, u"unavailable"_s}}).value(u"jobId").toString();
    const QJsonObject job = fixture.waitFinished(jobId);
    CHECK(job.value(u"state").toString() == u"failed"_s);
    CHECK(job.value(u"error").toString() == u"Ollama is not running."_s);
}

TEST_CASE("Jobs can be canceled before and after they reach the sidecar", "[services][commands]")
{
    AiFixture fixture;
    SECTION("waiting for the service")
    {
        const QString jobId = fixture.run(u"ai.generate_text"_s, {{u"prompt"_s, u"hold"_s}}).value(u"jobId").toString();
        const QJsonObject canceled = fixture.run(u"ai.cancel_job"_s, {{u"jobId"_s, jobId}});
        CHECK(canceled.value(u"state").toString() == u"canceled"_s);
        CHECK(fixture.job(jobId).value(u"state").toString() == u"canceled"_s);
    }
    SECTION("running in the sidecar")
    {
        const QString jobId = fixture.run(u"ai.generate_text"_s, {{u"prompt"_s, u"hold"_s}}).value(u"jobId").toString();
        REQUIRE(waitFor([&] { return !fixture.job(jobId).value(u"remoteId").toString().isEmpty(); }));
        fixture.run(u"ai.cancel_job"_s, {{u"jobId"_s, jobId}});
        CHECK(fixture.waitFinished(jobId).value(u"state").toString() == u"canceled"_s);

        auto again = fixture.bus.execute(u"ai.cancel_job"_s, {{u"jobId"_s, jobId}});
        REQUIRE_FALSE(again);
        CHECK(again.error().code == core::CommandErrorCode::NotAvailable);
    }
    auto unknown = fixture.bus.execute(u"ai.cancel_job"_s, {{u"jobId"_s, u"aijob-nope"_s}});
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().code == core::CommandErrorCode::Failed);
}

TEST_CASE("A sidecar crash fails the jobs it was running", "[services][commands]")
{
    QTemporaryDir counter;
    // The fake crashes when it is first polled, so the job is always in the sidecar by then.
    AiFixture fixture(
        {u"--mode"_s, u"crash-on-poll"_s, u"--crash-times"_s, u"1"_s, u"--counter"_s, counter.filePath(u"count"_s)});
    const QString jobId = fixture.run(u"ai.generate_text"_s, {{u"prompt"_s, u"hold"_s}}).value(u"jobId").toString();
    const QJsonObject job = fixture.waitFinished(jobId);
    CHECK_FALSE(job.value(u"remoteId").toString().isEmpty());
    CHECK(job.value(u"state").toString() == u"failed"_s);
    CHECK(job.value(u"error").toString() ==
          u"The AI service stopped while the job was running. Start the job again."_s);

    // The restarted service takes new jobs.
    const QString next = fixture.run(u"ai.generate_text"_s, {{u"prompt"_s, u"again"_s}}).value(u"jobId").toString();
    CHECK(fixture.waitFinished(next).value(u"state").toString() == u"succeeded"_s);
}

TEST_CASE("When the service cannot start, waiting jobs fail with its message", "[services][commands]")
{
    AiFixture fixture({}, true);
    const QString jobId = fixture.run(u"ai.generate_text"_s, {{u"prompt"_s, u"hi"_s}}).value(u"jobId").toString();
    const QJsonObject job = fixture.waitFinished(jobId);
    CHECK(job.value(u"state").toString() == u"failed"_s);
    CHECK(
        job.value(u"error").toString() ==
        u"The AI service could not start: its Python environment \"%1\" was not found. Create it as docs/development.md describes and restart Mikser Studio."_s
            .arg(QDir::toNativeSeparators(fixture.dir.filePath(u".venv"_s))));
    CHECK(fixture.run(u"ai.status"_s).value(u"state").toString() == u"failed"_s);

    auto backends = fixture.bus.execute(u"ai.backends"_s);
    REQUIRE_FALSE(backends);
    CHECK(backends.error().code == core::CommandErrorCode::NotAvailable);
    CHECK(backends.error().message.startsWith(u"The AI service could not start: its Python environment"_s));
}
