// The ai.* commands against a fake ComfyUI: ai.run brings images into the project with provenance and
// undo, a replaced project is left alone, and the backends, status, jobs, cancel and settings commands
// work on the engine.
#include "FakeAiTools.h"
#include "ServicesTestSupport.h"
#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/EventBus.h"
#include "mikserstudio/core/JobManager.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/core/ProjectCommands.h"
#include "mikserstudio/core/UndoStack.h"
#include "mikserstudio/services/AiCommands.h"
#include "mikserstudio/services/AiEngine.h"

#include <QJsonArray>
#include <QTemporaryDir>

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;
using namespace mikserstudio;
using namespace mikserstudio::services;
using mikserstudio::tests::FakeComfyUi;
using mikserstudio::tests::FakeProbe;
using mikserstudio::tests::waitFor;

namespace {

const QStringList kAiCommands{u"ai.run"_s,    u"ai.jobs"_s,     u"ai.cancel_job"_s,  u"ai.backends"_s,
                              u"ai.status"_s, u"ai.settings"_s, u"ai.set_settings"_s};

struct CommandsFixture {
    QTemporaryDir dir;
    core::UndoStack undo;
    core::CommandBus bus{undo};
    core::ProjectModel project;
    FakeProbe probe;
    FakeComfyUi comfy;
    std::unique_ptr<AiEngine> engine;
    std::unique_ptr<core::JobManager> jobs;

    CommandsFixture()
    {
        registerProjectCommands(bus, project, probe);
        engine = std::make_unique<AiEngine>(AiEngineOptions{.cacheDirectory = dir.filePath(u"cache"_s),
                                                            .settingsFile = dir.filePath(u"ai-settings.json"_s)});
        registerAiCommands(bus, project, *engine);
        jobs = std::make_unique<core::JobManager>(bus);
        core::registerJobCommands(bus, *jobs);
        mirrorAiJobs(*engine, *jobs);
        run(u"ai.set_settings"_s, {{u"comfyuiUrl"_s, comfy.url().toString()}});
    }

    QJsonObject run(const QString& id, const QJsonObject& params = {})
    {
        auto result = bus.execute(id, params);
        if (!result)
            FAIL(id.toStdString() << " failed: " << result.error().message.toStdString());
        return *result;
    }

    QString runOperation(const QString& op, const QString& prompt)
    {
        return run(u"ai.run"_s, {{u"op"_s, op}, {u"params"_s, QJsonObject{{u"prompt"_s, prompt}}}})
            .value(u"jobId")
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
    CommandsFixture fixture;
    for (const QString& id : kAiCommands) {
        const core::CommandSpec* spec = fixture.bus.spec(id);
        REQUIRE(spec);
        CHECK(spec->category == u"AI"_s);
        CHECK_FALSE(spec->undoable);
        CHECK(spec->readOnly == (id == u"ai.status"_s || id == u"ai.jobs"_s || id == u"ai.settings"_s));
    }
    const QJsonObject status = fixture.run(u"ai.status"_s);
    CHECK(status.value(u"activeJobs").toInt() == 0);
    CHECK(status.value(u"comfyuiUrl").toString() == fixture.comfy.url().toString());
    CHECK(fixture.run(u"ai.jobs"_s).value(u"jobs").toArray().isEmpty());
}

TEST_CASE("The commands go away with the engine", "[services][commands]")
{
    CommandsFixture fixture;
    fixture.jobs.reset();
    fixture.engine.reset();
    for (const QString& id : kAiCommands)
        CHECK_FALSE(fixture.bus.contains(id));
}

TEST_CASE("ai.run image.generate brings the image into the project with provenance, as one undo step",
          "[services][commands]")
{
    CommandsFixture fixture;
    const QString jobId = fixture.runOperation(u"image.generate"_s, u"A red fox in snow"_s);
    const QJsonObject job = fixture.waitFinished(jobId);
    INFO(job.value(u"error").toString().toStdString());
    REQUIRE(job.value(u"state").toString() == u"succeeded"_s);
    CHECK(job.value(u"title").toString() == u"Generate image: A red fox in snow"_s);

    const QJsonArray mediaIds = job.value(u"placement").toObject().value(u"mediaIds").toArray();
    REQUIRE(mediaIds.size() == 1);
    const core::MediaItem* media = fixture.project.findMedia(mediaIds.first().toString());
    REQUIRE(media);
    CHECK(media->name == u"A red fox in snow"_s);
    CHECK(media->path.endsWith(u"/%1/image-1.png"_s.arg(jobId)));
    CHECK(media->info.kind == core::MediaKind::Image);
    CHECK(media->provenance.value(u"jobId").toString() == jobId);
    CHECK(media->provenance.value(u"backend").toString() == u"comfyui"_s);
    CHECK(media->provenance.value(u"op").toString() == u"image.generate"_s);
    CHECK(media->provenance.value(u"params").toObject().value(u"prompt").toString() == u"A red fox in snow"_s);
    CHECK(media->provenance.value(u"params").toObject().contains(u"seed"));
    CHECK(media->provenance.value(u"tools").toObject().value(u"comfyui").toString() == u"0.9.1"_s);

    CHECK(fixture.undo.undoText() == u"Add AI result"_s);
    fixture.run(u"edit.undo"_s);
    CHECK(fixture.project.media().empty());
}

TEST_CASE("ai.run refuses an unknown operation or params the operation does not take", "[services][commands]")
{
    CommandsFixture fixture;
    const auto unknown = fixture.bus.execute(u"ai.run"_s, {{u"op"_s, u"video.upscale"_s}});
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().code == core::CommandErrorCode::InvalidParams);
    const auto noPrompt = fixture.bus.execute(u"ai.run"_s, {{u"op"_s, u"image.generate"_s}});
    REQUIRE_FALSE(noPrompt);
    CHECK(noPrompt.error().message.contains(u"prompt"_s));
}

TEST_CASE("A job that finishes after the project was replaced leaves the new project alone", "[services][commands]")
{
    CommandsFixture fixture;
    fixture.comfy.holdPrompts = true;
    const QString jobId = fixture.runOperation(u"image.generate"_s, u"A fox"_s);
    REQUIRE(waitFor([&] { return fixture.comfy.prompts().size() == 1; }));
    fixture.project.reset();
    fixture.comfy.run(u"prompt-1"_s);
    const QJsonObject job = fixture.waitFinished(jobId);
    CHECK(job.value(u"state").toString() == u"failed"_s);
    CHECK(job.value(u"error").toString().contains(u"replaced"_s));
    CHECK(fixture.project.media().empty());
}

TEST_CASE("ai.cancel_job cancels a running job", "[services][commands]")
{
    CommandsFixture fixture;
    fixture.comfy.holdPrompts = true;
    const QString jobId = fixture.runOperation(u"image.generate"_s, u"A fox"_s);
    REQUIRE(waitFor([&] { return fixture.comfy.prompts().size() == 1; }));
    CHECK(fixture.run(u"ai.status"_s).value(u"activeJobs").toInt() == 1);
    const QJsonObject canceled = fixture.run(u"ai.cancel_job"_s, {{u"jobId"_s, jobId}});
    CHECK(canceled.value(u"state").toString() == u"canceled"_s);
    CHECK(fixture.run(u"ai.status"_s).value(u"activeJobs").toInt() == 0);
    CHECK_FALSE(fixture.bus.execute(u"ai.cancel_job"_s, {{u"jobId"_s, jobId}}));
}

TEST_CASE("ai.backends lists the backends and checks them in the background", "[services][commands]")
{
    CommandsFixture fixture;
    const auto backends = [&] { return fixture.run(u"ai.backends"_s).value(u"backends").toArray(); };
    REQUIRE(backends().size() == 1);
    REQUIRE(waitFor([&] {
        const QJsonArray list = backends();
        return std::ranges::all_of(
            list, [](const QJsonValue& backend) { return backend.toObject().value(u"available").toBool(); });
    }));
    const QJsonArray list = backends();
    CHECK(list.at(0).toObject().value(u"id").toString() == u"comfyui"_s);
    const QJsonObject op = list.at(0).toObject().value(u"ops").toArray().first().toObject();
    CHECK(op.value(u"id").toString() == u"image.generate"_s);
    CHECK(op.value(u"paramsSchema").toObject().value(u"required").toArray() == QJsonArray{u"prompt"_s});
}

TEST_CASE("ai.set_settings changes the address and refuses one that is not http", "[services][commands]")
{
    CommandsFixture fixture;
    const QJsonObject changed = fixture.run(u"ai.set_settings"_s, {{u"comfyuiUrl"_s, u"http://127.0.0.1:8189"_s}});
    CHECK(changed == QJsonObject{{u"comfyuiUrl"_s, u"http://127.0.0.1:8189"_s}});
    CHECK(fixture.run(u"ai.settings"_s) == changed);

    // Ollama's address is the Ollama plugin's setting now.
    const auto ollama = fixture.bus.execute(u"ai.set_settings"_s, {{u"ollamaUrl"_s, u"http://127.0.0.1:11500"_s}});
    REQUIRE_FALSE(ollama);
    CHECK(ollama.error().code == core::CommandErrorCode::InvalidParams);

    const auto refused = fixture.bus.execute(u"ai.set_settings"_s, {{u"comfyuiUrl"_s, u"file:///etc"_s}});
    REQUIRE_FALSE(refused);
    CHECK(refused.error().code == core::CommandErrorCode::InvalidParams);
    CHECK(fixture.run(u"ai.settings"_s) == changed);
}

TEST_CASE("AI jobs are core jobs: jobs.list lists them and job events report them", "[services][commands][jobs]")
{
    CommandsFixture fixture;
    QList<core::Event> events;
    QObject::connect(&fixture.bus.events(), &core::EventBus::published, &fixture.bus,
                     [&events](const core::Event& event) {
                         if (event.topic.startsWith(u"job."_s))
                             events.append(event);
                     });
    const auto run = fixture.bus.execute(
        u"ai.run"_s, {{u"op"_s, u"image.generate"_s}, {u"params"_s, QJsonObject{{u"prompt"_s, u"A fox"_s}}}},
        core::CallContext::api());
    REQUIRE(run);
    const QString jobId = run->value(u"jobId"_s).toString();
    const QJsonObject aiJob = fixture.waitFinished(jobId);
    REQUIRE(aiJob.value(u"state"_s).toString() == u"succeeded"_s);

    const QJsonArray list = fixture.run(u"jobs.list"_s).value(u"jobs"_s).toArray();
    REQUIRE(list.size() == 1);
    const QJsonObject job = list.at(0).toObject();
    CHECK(job.value(u"id"_s) == jobId);
    CHECK(job.value(u"kind"_s) == u"ai.image.generate"_s);
    CHECK(job.value(u"lane"_s) == u"ai:comfyui"_s);
    CHECK(job.value(u"state"_s) == u"succeeded"_s);
    CHECK(job.value(u"result"_s).toObject().value(u"result"_s) == aiJob.value(u"result"_s));

    REQUIRE(events.size() >= 2);
    CHECK(events.constFirst().topic == u"job.created"_s);
    CHECK(events.constFirst().origins == QStringList{u"api"_s});
    CHECK(events.constLast().topic == u"job.finished"_s);
    CHECK(events.constLast().payload.value(u"state"_s) == u"succeeded"_s);
}

TEST_CASE("jobs.cancel cancels an AI job in the engine", "[services][commands][jobs]")
{
    CommandsFixture fixture;
    fixture.comfy.holdPrompts = true;
    const QString jobId = fixture.runOperation(u"image.generate"_s, u"A slow fox"_s);
    REQUIRE(waitFor([&] { return fixture.comfy.prompts().size() == 1; }));
    const auto canceled = fixture.bus.execute(u"jobs.cancel"_s, QJsonObject{{u"jobId"_s, jobId}});
    REQUIRE(canceled);
    CHECK(canceled->value(u"state"_s) == u"canceled"_s);
    CHECK(fixture.job(jobId).value(u"state"_s).toString() == u"canceled"_s);
    CHECK(fixture.run(u"ai.status"_s).value(u"activeJobs").toInt() == 0);
}
