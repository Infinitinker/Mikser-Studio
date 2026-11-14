// A plugin's own jobs (ms.jobs.run): their function's result, progress, children and cancel, the
// spec they need, awaiting a job by its id, and the network entries the jobs a plugin starts get.
#include "LuaTestSupport.h"

#include <catch2/catch_test_macros.hpp>

#include <QJsonArray>
#include <QTimer>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using mikserstudio::tests::LogCapture;
using mikserstudio::tests::LuaFixture;
using mikserstudio::tests::waitFor;

namespace {

namespace core = mikserstudio::core;

// "test.wait" {value} succeeds with its input after 50 ms; "test.hold" never ends by itself.
void registerKinds(core::JobManager& jobs, QStringList* network = nullptr)
{
    jobs.registerKind(
        u"test.wait"_s,
        core::JobKind{.permission = u"project.read"_s,
                      .start = [&jobs, network](core::JobManager&, core::JobSpec spec, const QJsonObject& input) {
                          if (network)
                              *network = spec.network;
                          return jobs.submit(std::move(spec), [&jobs, input](const QString& id) {
                              QTimer::singleShot(50ms, &jobs, [&jobs, id, input] { jobs.succeed(id, input); });
                          });
                      }});
    jobs.registerKind(u"test.hold"_s,
                      core::JobKind{.permission = u"project.read"_s,
                                    .start = [&jobs](core::JobManager&, core::JobSpec spec, const QJsonObject&) {
                                        return jobs.submit(std::move(spec), {});
                                    }});
}

QJsonObject jobNamed(const core::JobManager& jobs, const QString& id)
{
    for (const QJsonValue value : jobs.jobsJson()) {
        if (value.toObject().value(u"id"_s).toString() == id)
            return value.toObject();
    }
    return {};
}

bool ended(const core::JobManager& jobs, const QString& id)
{
    return jobs.state(id).value_or(core::JobState::Running) > core::JobState::Running;
}

const QByteArray kPlugin = R"lua(
    ms.command{
        id = "test.plugin.work",
        title = "Work",
        params = { type = "object", properties = { kind = { type = "string" }, fail = { type = "boolean" } } },
        handler = function(params)
            local job = ms.jobs.run({ kind = "work", title = "Work it", lane = "one" }, function(job)
                job:progress(0.25, "Starting")
                if params.fail then error("no luck") end
                local child = ms.jobs.start(params.kind or "test.wait", { value = 7 })
                return { value = child:await().value * 2, child = child.id }
            end)
            return { jobId = job.id }
        end,
    }
    ms.command{
        id = "test.plugin.await",
        title = "Await",
        params = { type = "object", properties = { id = { type = "string" } } },
        handler = function(params)
            local ok, result = pcall(function() return ms.jobs.get(params.id):await() end)
            return ok and { result = result } or { code = result.code, message = result.message }
        end,
    }
    ms.command{
        id = "test.plugin.bad",
        title = "Bad",
        handler = function()
            local _, untitled = pcall(ms.jobs.run, { kind = "work" }, function() end)
            local _, unkind = pcall(ms.jobs.run, { kind = "Not A Kind", title = "Work" }, function() end)
            local _, nothing = pcall(ms.jobs.run, { kind = "work", title = "Work" })
            local _, progress = pcall(function() ms.jobs.get("job-x"):progress(0.5) end)
            return {
                untitled = untitled.code,
                unkind = unkind.code,
                nothing = tostring(nothing),
                progress = tostring(progress),
            }
        end,
    }
)lua";

}  // namespace

TEST_CASE("A plugin's own job runs its function and ends with its result", "[lua][jobs]")
{
    LuaFixture f;
    registerKinds(f.jobs);
    REQUIRE(f.activate(kPlugin));
    const auto started = f.run(u"test.plugin.work"_s);
    REQUIRE(started);
    const QString id = started->value(u"jobId"_s).toString();
    REQUIRE(waitFor([&] { return ended(f.jobs, id); }));

    const QJsonObject job = jobNamed(f.jobs, id);
    CHECK(job.value(u"state"_s).toString() == u"succeeded"_s);
    CHECK(job.value(u"kind"_s).toString() == u"test.plugin.work"_s);
    CHECK(job.value(u"title"_s).toString() == u"Work it"_s);
    CHECK(job.value(u"owner"_s).toString() == u"test.plugin"_s);
    CHECK(job.value(u"lane"_s).toString() == u"test.plugin/one"_s);
    CHECK(job.value(u"origins"_s).toArray() == QJsonArray{u"api"_s, u"plugin:test.plugin"_s});
    CHECK(job.value(u"result"_s).toObject().value(u"value"_s).toInt() == 14);
    // The child shows its parent; the parent showed its progress.
    const QJsonObject child = jobNamed(f.jobs, job.value(u"result"_s).toObject().value(u"child"_s).toString());
    CHECK(child.value(u"parent"_s).toString() == id);
    CHECK(child.value(u"origins"_s).toArray() == QJsonArray{u"api"_s, u"plugin:test.plugin"_s});
    CHECK(job.value(u"message"_s).toString() == u"Starting"_s);

    // Awaiting it by its id once it has ended gives its result.
    const auto awaited = f.run(u"test.plugin.await"_s, {{u"id"_s, id}});
    REQUIRE(awaited);
    CHECK(awaited->value(u"result"_s).toObject().value(u"value"_s).toInt() == 14);
}

TEST_CASE("An error in a job's function fails the job with its message", "[lua][jobs]")
{
    LuaFixture f;
    registerKinds(f.jobs);
    REQUIRE(f.activate(kPlugin));
    const QString id = f.run(u"test.plugin.work"_s, {{u"fail"_s, true}})->value(u"jobId"_s).toString();
    REQUIRE(waitFor([&] { return ended(f.jobs, id); }));
    const QJsonObject job = jobNamed(f.jobs, id);
    CHECK(job.value(u"state"_s).toString() == u"failed"_s);
    CHECK(job.value(u"error"_s).toString().contains(u"no luck"_s));
}

TEST_CASE("Canceling a plugin's job cancels its children, and its function stops at its next wait", "[lua][jobs]")
{
    LuaFixture f;
    registerKinds(f.jobs);
    const LogCapture log;
    REQUIRE(f.activate(kPlugin));
    const QString id = f.run(u"test.plugin.work"_s, {{u"kind"_s, u"test.hold"_s}})->value(u"jobId"_s).toString();
    QString child;
    REQUIRE(waitFor([&] {
        for (const QJsonValue value : f.jobs.jobsJson()) {
            if (value.toObject().value(u"parent"_s).toString() == id)
                child = value.toObject().value(u"id"_s).toString();
        }
        return !child.isEmpty();
    }));
    REQUIRE(f.jobs.cancel(id) == core::JobState::Canceled);
    CHECK(f.jobs.state(child) == core::JobState::Canceled);

    // The function's await raises "canceled", which ends it quietly; another job still runs.
    const QString next = f.run(u"test.plugin.work"_s)->value(u"jobId"_s).toString();
    REQUIRE(waitFor([&] { return ended(f.jobs, next); }));
    CHECK(f.jobs.state(next) == core::JobState::Succeeded);
    CHECK(f.jobs.state(id) == core::JobState::Canceled);
    CHECK_FALSE(log.text().contains(u"stack traceback"_s));
}

TEST_CASE("Jobs of one lane of a plugin run one at a time", "[lua][jobs]")
{
    LuaFixture f;
    registerKinds(f.jobs);
    REQUIRE(f.activate(kPlugin));
    const QString first = f.run(u"test.plugin.work"_s, {{u"kind"_s, u"test.hold"_s}})->value(u"jobId"_s).toString();
    const QString second = f.run(u"test.plugin.work"_s)->value(u"jobId"_s).toString();
    QCoreApplication::processEvents();
    CHECK(f.jobs.state(second) == core::JobState::Queued);
    REQUIRE(f.jobs.cancel(first));
    REQUIRE(waitFor([&] { return ended(f.jobs, second); }));
    CHECK(f.jobs.state(second) == core::JobState::Succeeded);
}

TEST_CASE("ms.jobs.run needs a kind, a title and a function; only its job takes progress", "[lua][jobs]")
{
    LuaFixture f;
    REQUIRE(f.activate(kPlugin));
    const auto answer = f.run(u"test.plugin.bad"_s);
    REQUIRE(answer);
    CHECK(answer->value(u"untitled"_s).toString() == u"invalid_params"_s);
    CHECK(answer->value(u"unkind"_s).toString() == u"invalid_params"_s);
    CHECK(answer->value(u"nothing"_s).toString().contains(u"ms.jobs.run takes"_s));
    CHECK(answer->value(u"progress"_s).toString().contains(u"job:progress works only"_s));
    CHECK(f.jobs.jobsJson().isEmpty());
}

TEST_CASE("A job is awaited by its id until it ends, and an unknown id is refused", "[lua][jobs]")
{
    LuaFixture f;
    core::registerJobCommands(f.bus, f.jobs);
    REQUIRE(f.activate(kPlugin));
    const auto id = f.jobs.submit(
        core::JobSpec{.id = {}, .kind = u"test.outside"_s, .title = u"Outside"_s, .owner = u"core"_s, .lane = {}}, {});
    REQUIRE(id);
    QTimer::singleShot(200ms, &f.jobs, [&f, &id] { f.jobs.succeed(*id, {{u"done"_s, true}}); });
    const auto awaited = f.run(u"test.plugin.await"_s, {{u"id"_s, *id}});
    REQUIRE(awaited);
    CHECK(awaited->value(u"result"_s).toObject().value(u"done"_s).toBool());

    const auto unknown = f.run(u"test.plugin.await"_s, {{u"id"_s, u"job-nothing"_s}});
    REQUIRE(unknown);
    CHECK(unknown->value(u"code"_s).toString() == u"invalid_params"_s);
}

TEST_CASE("The jobs a plugin starts get its network entries", "[lua][jobs]")
{
    LuaFixture f;
    QStringList network;
    registerKinds(f.jobs, &network);
    f.network = {u"*:443"_s, u"127.0.0.1:11434"_s};
    REQUIRE(f.activate(kPlugin));
    const QString id = f.run(u"test.plugin.work"_s)->value(u"jobId"_s).toString();
    REQUIRE(waitFor([&] { return ended(f.jobs, id); }));
    CHECK(network == f.network);
}
