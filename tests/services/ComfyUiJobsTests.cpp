// The comfyui.run_prompt job kind against the fake ComfyUI: queuing a workflow, following it and
// copying its files, a refused workflow, ComfyUI's failures, canceling only the job's own prompt, the
// network entries, and closing the app.
#include "FakeAiTools.h"
#include "ServicesTestSupport.h"
#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/JobManager.h"
#include "mikserstudio/core/UndoStack.h"
#include "mikserstudio/services/ComfyUiJobs.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QTemporaryDir>

#include <memory>
#include <optional>

using namespace Qt::StringLiterals;
using namespace mikserstudio;
using namespace mikserstudio::services;
using mikserstudio::tests::FakeComfyUi;
using mikserstudio::tests::waitFor;

namespace {

const auto kKind = u"comfyui.run_prompt"_s;

struct ComfyFixture {
    QTemporaryDir dir;
    core::UndoStack undo;
    core::CommandBus bus{undo};
    core::JobManager jobs{bus};
    std::optional<quint16> apiPort;
    std::unique_ptr<ComfyUiJobs> comfyJobs = std::make_unique<ComfyUiJobs>(
        jobs, ComfyUiJobOptions{.apiPort = [this] { return apiPort; }, .outputDirectory = dir.filePath(u"jobs"_s)});
    FakeComfyUi comfy;
    QStringList network{u"127.0.0.1:*"_s};

    // A workflow of one node that saves, as ComfyUI's SaveImage does.
    static QJsonObject workflow()
    {
        return QJsonObject{
            {u"9"_s, QJsonObject{{u"class_type"_s, u"SaveImage"_s},
                                 {u"inputs"_s, QJsonObject{{u"filename_prefix"_s, u"mikserstudio/test"_s}}}}}};
    }

    QJsonObject input() const
    {
        return QJsonObject{{u"url"_s, comfy.url().toString()},
                           {u"workflow"_s, workflow()},
                           {u"outputNode"_s, u"9"_s},
                           {u"stages"_s, QJsonObject{{u"9"_s, u"Saving the image"_s}}}};
    }

    std::expected<QString, core::CommandError> start(const QJsonObject& in)
    {
        return jobs.start(kKind, in,
                          core::CallContext{.origins = {u"plugin:test.plugin"_s},
                                            .permissions = core::allPermissions(),
                                            .confirmed = false},
                          core::JobRequest{.parent = {}, .network = network});
    }

    QString started()
    {
        const auto id = start(input());
        if (!id)
            FAIL("start failed: " << id.error().message.toStdString());
        return *id;
    }

    QJsonObject job(const QString& id) const
    {
        for (const QJsonValue value : jobs.jobsJson()) {
            if (value.toObject().value(u"id").toString() == id)
                return value.toObject();
        }
        return {};
    }

    QJsonObject finished(const QString& id) const
    {
        REQUIRE(waitFor([&] { return jobs.state(id).value_or(core::JobState::Running) > core::JobState::Running; }));
        return job(id);
    }
};

}  // namespace

TEST_CASE("comfyui.run_prompt queues the workflow, follows it and copies its files", "[services][comfyui]")
{
    ComfyFixture f;
    f.comfy.imageCount = 2;
    const QString id = f.started();
    const QJsonObject job = f.finished(id);
    INFO(job.value(u"error").toString().toStdString());
    REQUIRE(job.value(u"state").toString() == u"succeeded"_s);
    CHECK(job.value(u"title").toString() == u"ComfyUI: run a workflow"_s);

    REQUIRE(f.comfy.prompts().size() == 1);
    CHECK(f.comfy.prompts().first().second == ComfyFixture::workflow());
    const QJsonObject result = job.value(u"result").toObject();
    const QString promptId = f.comfy.prompts().first().first;
    CHECK(result.value(u"promptId").toString() == promptId);
    const QJsonArray outputs = result.value(u"outputs").toArray();
    REQUIRE(outputs.size() == 2);
    for (const QJsonValue output : outputs) {
        const QFileInfo file(output.toObject().value(u"path").toString());
        CHECK(file.exists());
        CHECK(file.size() == f.comfy.image.size());
        CHECK(file.absolutePath() == QFileInfo(f.dir.filePath(u"jobs/"_s + id)).absoluteFilePath());
        CHECK(output.toObject().value(u"kind").toString() == u"image"_s);
    }
    CHECK(result.value(u"provenance").toObject() ==
          QJsonObject{{u"promptId"_s, promptId}, {u"comfyui"_s, f.comfy.url().toString()}});
    CHECK(job.value(u"progress").toDouble() == 1.0);
}

TEST_CASE("Events ComfyUI sends before it answers the prompt are followed", "[services][comfyui]")
{
    ComfyFixture f;
    f.comfy.runEarly = true;
    CHECK(f.finished(f.started()).value(u"state").toString() == u"succeeded"_s);
}

TEST_CASE("A workflow ComfyUI refuses ends with ComfyUI's answer", "[services][comfyui]")
{
    ComfyFixture f;
    f.comfy.refusePrompts = true;
    const QJsonObject job = f.finished(f.started());
    REQUIRE(job.value(u"state").toString() == u"succeeded"_s);
    const QJsonObject refused = job.value(u"result").toObject().value(u"refused").toObject();
    CHECK(refused.value(u"status").toInt() == 400);
    CHECK(refused.value(u"json").toObject().contains(u"node_errors"_s));
    CHECK_FALSE(refused.value(u"body").toString().isEmpty());
}

TEST_CASE("ComfyUI's errors, a stop in ComfyUI and a closed socket fail the job", "[services][comfyui]")
{
    ComfyFixture f;
    f.comfy.holdPrompts = true;

    const QString failing = f.started();
    REQUIRE(waitFor([&] { return f.comfy.prompts().size() == 1; }));
    f.comfy.fail(f.comfy.prompts().at(0).first, u"Out of memory"_s);
    const QJsonObject failed = f.finished(failing);
    CHECK(failed.value(u"state").toString() == u"failed"_s);
    CHECK(failed.value(u"error").toString().startsWith(u"ComfyUI stopped with Out of memory"_s));

    const QString stopped = f.started();
    REQUIRE(waitFor([&] { return f.comfy.prompts().size() == 2; }));
    f.comfy.interrupt(f.comfy.prompts().at(1).first);
    CHECK(f.finished(stopped).value(u"error").toString() == u"The job was stopped in ComfyUI."_s);

    const QString closed = f.started();
    REQUIRE(waitFor([&] { return f.comfy.prompts().size() == 3; }));
    f.comfy.closeSockets();
    CHECK(f.finished(closed).value(u"error").toString().startsWith(u"ComfyUI closed the connection"_s));
}

TEST_CASE("A ComfyUI that isn't running fails the job at once", "[services][comfyui]")
{
    ComfyFixture f;
    QJsonObject in = f.input();
    in.insert(u"url"_s, u"http://127.0.0.1:9"_s);
    const auto id = f.start(in);
    REQUIRE(id);
    const QJsonObject job = f.finished(*id);
    CHECK(job.value(u"state").toString() == u"failed"_s);
    CHECK(job.value(u"error").toString().startsWith(u"ComfyUI is not reachable at http://127.0.0.1:9"_s));
}

TEST_CASE("Canceling takes only the job's own prompt off ComfyUI's queue", "[services][comfyui]")
{
    ComfyFixture f;
    f.comfy.holdPrompts = true;
    const QString id = f.started();
    REQUIRE(waitFor([&] { return f.comfy.prompts().size() == 1; }));
    REQUIRE(f.jobs.cancel(id) == core::JobState::Canceled);
    REQUIRE(waitFor([&] { return f.comfy.deleted() == QStringList{f.comfy.prompts().first().first}; }));
    CHECK_FALSE(f.comfy.wasAsked(u"/interrupt"_s));
    CHECK_FALSE(f.comfy.wasAsked(u"/free"_s));

    // A canceled job's late events change nothing.
    f.comfy.run(f.comfy.prompts().first().first);
    tests::spin(std::chrono::milliseconds(200));
    CHECK(f.job(id).value(u"state").toString() == u"canceled"_s);
}

TEST_CASE("comfyui.run_prompt is refused outside the network entries and with bad input", "[services][comfyui]")
{
    ComfyFixture f;
    f.network = {u"comfy.example.com:8188"_s};
    const auto outside = f.start(f.input());
    REQUIRE_FALSE(outside);
    CHECK(outside.error().code == core::CommandErrorCode::PermissionDenied);

    f.network = {u"127.0.0.1:*"_s};
    f.apiPort = static_cast<quint16>(f.comfy.url().port());
    const auto api = f.start(f.input());
    REQUIRE_FALSE(api);
    CHECK(api.error().code == core::CommandErrorCode::PermissionDenied);
    f.apiPort.reset();

    QJsonObject ftp = f.input();
    ftp.insert(u"url"_s, u"ftp://127.0.0.1:8188"_s);
    CHECK(f.start(ftp).error().code == core::CommandErrorCode::InvalidParams);
    QJsonObject noNode = f.input();
    noNode.remove(u"outputNode"_s);
    CHECK(f.start(noNode).error().code == core::CommandErrorCode::InvalidParams);
    CHECK(f.comfy.prompts().isEmpty());
}

TEST_CASE("When the ComfyUI jobs go, their kind goes, their jobs fail and their prompts leave the queue",
          "[services][comfyui]")
{
    ComfyFixture f;
    f.comfy.holdPrompts = true;
    const QString id = f.started();
    REQUIRE(waitFor([&] { return f.comfy.prompts().size() == 1; }));
    f.comfyJobs.reset();
    const QJsonObject job = f.job(id);
    CHECK(job.value(u"state").toString() == u"failed"_s);
    CHECK(job.value(u"error").toString().contains(u"closing"_s));
    CHECK(f.comfy.deleted() == QStringList{f.comfy.prompts().first().first});
    const auto gone = f.start(f.input());
    REQUIRE_FALSE(gone);
    CHECK(gone.error().code == core::CommandErrorCode::InvalidParams);
}
