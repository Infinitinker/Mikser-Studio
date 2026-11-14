// The job manager: lanes run one job at a time in order, progress, cancel and results, the jobs.*
// commands, the job.* events with their origins, and forgetting old jobs.
#include "TestSupport.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/EventBus.h"
#include "mikserstudio/core/JobManager.h"
#include "mikserstudio/core/UndoStack.h"

#include <catch2/catch_test_macros.hpp>

#include <QJsonArray>
#include <QThread>

using namespace Qt::StringLiterals;
using namespace mikserstudio::core;

namespace {

struct Fixture {
    Fixture()
    {
        registerJobCommands(bus, jobs);
        QObject::connect(&bus.events(), &EventBus::published, &bus, [this](const Event& event) {
            if (event.topic.startsWith(u"job."_s))
                events.append(event);
        });
    }

    QString submit(const QString& lane, const QString& title = u"Work"_s)
    {
        const auto id = jobs.submit(
            {.id = {}, .kind = u"test.work"_s, .title = title, .owner = u"core"_s, .lane = lane},
            [this](const QString& jobId) { started.append(jobId); },
            [this](const QString& jobId) { stopped.append(jobId); });
        REQUIRE(id);
        return *id;
    }

    UndoStack stack;
    CommandBus bus{stack};
    JobManager jobs{bus};
    QStringList started;
    QStringList stopped;
    QList<Event> events;
};

}  // namespace

TEST_CASE("Jobs of one lane run one at a time in order, other lanes side by side", "[jobs]")
{
    Fixture f;
    const QString first = f.submit(u"ai:comfyui"_s);
    const QString second = f.submit(u"ai:comfyui"_s);
    const QString other = f.submit(u"ai:ollama"_s);
    const QString free = f.submit({});
    CHECK(f.started == QStringList{first, other, free});
    CHECK(f.jobs.state(second) == JobState::Queued);

    f.jobs.succeed(first, QJsonObject{{u"mediaId"_s, u"media-1"_s}});
    CHECK(f.jobs.state(first) == JobState::Succeeded);
    CHECK(f.started == QStringList{first, other, free, second});
    f.jobs.fail(second, u"The tool stopped."_s);
    CHECK(f.jobs.state(second) == JobState::Failed);

    const QJsonArray list = f.jobs.jobsJson();
    REQUIRE(list.size() == 4);
    const QJsonObject done = list.at(0).toObject();
    CHECK(done.value(u"id"_s) == first);
    CHECK(done.value(u"state"_s) == u"succeeded"_s);
    CHECK(done.value(u"progress"_s) == 1.0);
    CHECK(done.value(u"result"_s).toObject().value(u"mediaId"_s) == u"media-1"_s);
    CHECK(list.at(1).toObject().value(u"error"_s) == u"The tool stopped."_s);
    CHECK(list.at(2).toObject().value(u"state"_s) == u"running"_s);

    CHECK_FALSE(f.jobs.submit({.id = first, .kind = u"test.work"_s, .title = {}, .owner = {}, .lane = {}}, {}));
}

TEST_CASE("Canceling a job stops its work and ignores what it reports later", "[jobs]")
{
    Fixture f;
    const QString running = f.submit(u"lane"_s);
    const QString queued = f.submit(u"lane"_s);

    const auto canceledQueued = f.jobs.cancel(queued);
    REQUIRE(canceledQueued);
    CHECK(*canceledQueued == JobState::Canceled);
    CHECK(f.stopped == QStringList{queued});

    REQUIRE(f.jobs.cancel(running));
    CHECK(f.stopped == QStringList{queued, running});
    f.jobs.succeed(running);
    f.jobs.progress(running, 0.5);
    CHECK(f.jobs.state(running) == JobState::Canceled);
    CHECK(f.started == QStringList{running});  // The canceled queued job never starts.

    const auto again = f.jobs.cancel(running);
    REQUIRE(again);
    CHECK(*again == JobState::Canceled);
    CHECK(f.stopped.size() == 2);
    CHECK_FALSE(f.jobs.cancel(u"job-none"_s));
}

TEST_CASE("Job events carry the job's id and the origins of the command that started it", "[jobs][events]")
{
    Fixture f;
    CommandSpec start;
    start.id = u"test.start_job"_s;
    start.title = u"Start a job"_s;
    QString id;
    REQUIRE(f.bus.registerCommand(start, [&f, &id](const QJsonObject&) -> CommandResult {
        id = f.submit({});
        return CommandOutcome{QJsonObject{{u"jobId"_s, id}}, nullptr};
    }));
    REQUIRE(f.bus.execute(u"test.start_job"_s, {}, CallContext::api()));

    f.jobs.progress(id, 0.25, u"Step 1 of 4"_s);
    f.jobs.progress(id, 0.5, u"Step 2 of 4"_s);  // Within the interval: no event.
    QThread::sleep(JobManager::kProgressInterval + std::chrono::milliseconds(20));
    f.jobs.progress(id, 0.75, u"Step 3 of 4"_s);
    f.jobs.succeed(id, QJsonObject{{u"done"_s, true}});

    QStringList topics;
    for (const Event& event : f.events) {
        topics.append(event.topic);
        CHECK(event.payload.value(u"jobId"_s) == id);
        CHECK(event.origins == QStringList{u"api"_s});
    }
    CHECK(topics == QStringList{u"job.created"_s, u"job.progress"_s, u"job.progress"_s, u"job.finished"_s});
    CHECK(f.events.at(0).payload.value(u"kind"_s) == u"test.work"_s);
    CHECK(f.events.at(1).payload.value(u"progress"_s) == 0.25);
    CHECK(f.events.at(2).payload.value(u"message"_s) == u"Step 3 of 4"_s);
    CHECK(f.events.at(3).payload.value(u"state"_s) == u"succeeded"_s);
    CHECK(f.events.at(3).payload.value(u"result"_s).toObject().value(u"done"_s).toBool());
}

TEST_CASE("jobs.list and jobs.cancel run the job manager as commands", "[jobs][commands]")
{
    Fixture f;
    const QString id = f.submit(u"lane"_s, u"Generate image"_s);
    const auto list = f.bus.execute(u"jobs.list"_s);
    REQUIRE(list);
    REQUIRE(list->value(u"jobs"_s).toArray().size() == 1);
    CHECK(list->value(u"jobs"_s).toArray().at(0).toObject().value(u"title"_s) == u"Generate image"_s);
    CHECK(f.bus.spec(u"jobs.list"_s)->readOnly);

    const auto canceled = f.bus.execute(u"jobs.cancel"_s, QJsonObject{{u"jobId"_s, id}});
    REQUIRE(canceled);
    CHECK(canceled->value(u"state"_s) == u"canceled"_s);
    const auto missing = f.bus.execute(u"jobs.cancel"_s, QJsonObject{{u"jobId"_s, u"job-none"_s}});
    REQUIRE_FALSE(missing);
    CHECK(missing.error().code == CommandErrorCode::InvalidParams);
}

TEST_CASE("The job manager keeps the last finished jobs and every unfinished one", "[jobs]")
{
    Fixture f;
    const QString waiting = f.submit(u"slow"_s);
    for (std::size_t i = 0; i < JobManager::kMaxFinishedJobs + 5; ++i)
        f.jobs.succeed(f.submit({}));
    const QJsonArray list = f.jobs.jobsJson();
    CHECK(list.size() == static_cast<qsizetype>(JobManager::kMaxFinishedJobs + 1));
    CHECK(list.at(0).toObject().value(u"id"_s) == waiting);
}

TEST_CASE("A job of a registered kind starts for its caller, who owns it")
{
    UndoStack undo;
    CommandBus bus(undo);
    JobManager jobs(bus);
    QJsonObject given;
    jobs.registerKind(u"test.echo"_s,
                      JobKind{.permission = u"network"_s,
                              .start = [&given](JobManager& manager, JobSpec spec, const QJsonObject& input) {
                                  given = input;
                                  spec.title = u"Echo"_s;
                                  return manager.submit(std::move(spec), [&manager](const QString& id) {
                                      manager.succeed(id, {{u"ok"_s, true}});
                                  });
                              }});
    const CallContext plugin{.origins = {u"api"_s, u"plugin:test.echo"_s}, .permissions = {u"network"_s}};

    const auto id = jobs.start(u"test.echo"_s, {{u"text"_s, u"hi"_s}}, plugin);
    REQUIRE(id);
    CHECK(given.value(u"text"_s).toString() == u"hi"_s);
    const QJsonObject job = jobs.jobsJson().first().toObject();
    CHECK(job.value(u"kind"_s).toString() == u"test.echo"_s);
    CHECK(job.value(u"owner"_s).toString() == u"test.echo"_s);
    CHECK(job.value(u"origins"_s).toArray() == QJsonArray{u"api"_s, u"plugin:test.echo"_s});
    CHECK(job.value(u"state"_s).toString() == u"succeeded"_s);

    const CallContext without{.origins = {u"plugin:test.other"_s}, .permissions = {u"project.read"_s}};
    CHECK(jobs.start(u"test.echo"_s, {}, without).error().code == CommandErrorCode::PermissionDenied);
    CHECK(jobs.start(u"test.missing"_s, {}, plugin).error().code == CommandErrorCode::InvalidParams);
    CHECK(jobs.jobsJson().size() == 1);
}
