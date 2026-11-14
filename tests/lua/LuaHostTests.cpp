// The Lua host: plugins in sandboxed states on their own threads, the ms API, the limits and the
// hot reload.
#include "LuaTestSupport.h"

#include "mikserstudio/core/ProjectSnapshot.h"

#include <catch2/catch_test_macros.hpp>

#include <QElapsedTimer>
#include <QJsonArray>
#include <QTimer>

#include <vector>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;
using mikserstudio::core::CommandErrorCode;
using mikserstudio::tests::LogCapture;
using mikserstudio::tests::LuaFixture;
using mikserstudio::tests::waitFor;

namespace {

namespace core = mikserstudio::core;

class CountEdit final : public core::UndoableEdit {
public:
    explicit CountEdit(int& count)
        : m_count(count)
    {
    }
    void undo() override { --m_count; }
    void redo() override { ++m_count; }
    [[nodiscard]] QString text() const override { return u"Count"_s; }

private:
    int& m_count;
};

core::CommandSpec testSpec(const QString& id)
{
    core::CommandSpec spec;
    spec.id = id;
    spec.title = id;
    spec.category = u"Test"_s;
    return spec;
}

// Commands the plugins under test run.
struct TestCommands {
    explicit TestCommands(core::CommandBus& bus)
    {
        core::CommandSpec count = testSpec(u"test.count"_s);
        count.undoable = true;
        bus.registerCommand(count, [this](const QJsonObject&) -> core::CommandResult {
            ++counted;
            return core::CommandOutcome{{{u"count"_s, counted}}, std::make_unique<CountEdit>(counted)};
        });

        core::CommandSpec replace = testSpec(u"test.replace"_s);
        replace.confirmationParam = u"overwrite"_s;
        replace.paramsSchema = QJsonObject{
            {u"type"_s, u"object"_s},
            {u"properties"_s,
             QJsonObject{{u"overwrite"_s, QJsonObject{{u"type"_s, u"boolean"_s}, {u"default"_s, false}}}}}};
        bus.registerCommand(replace, [](const QJsonObject& params) -> core::CommandResult {
            if (!params.value(u"overwrite").toBool())
                return core::commandFailure(CommandErrorCode::NeedsConfirmation, u"Replace it?"_s);
            return core::CommandOutcome{{{u"replaced"_s, true}}, nullptr};
        });

        core::CommandSpec ask = testSpec(u"test.ask"_s);
        ask.confirmation = u"Really?"_s;
        bus.registerCommand(ask, [](const QJsonObject&) -> core::CommandResult { return core::CommandOutcome{}; });

        core::CommandSpec record = testSpec(u"test.record"_s);
        record.paramsSchema = QJsonObject{
            {u"type"_s, u"object"_s},
            {u"properties"_s, QJsonObject{{u"value"_s, QJsonObject{{u"type"_s, u"integer"_s}}},
                                          {u"topic"_s, QJsonObject{{u"type"_s, u"string"_s}}}}}};
        bus.registerCommand(record, [this](const QJsonObject& params) -> core::CommandResult {
            recorded.push_back(params);
            return core::CommandOutcome{};
        });

        // Answers after the plugin's reload (whose debounce is 300 ms) is done.
        bus.registerAsyncCommand(testSpec(u"test.slow"_s),
                                 [this](const QJsonObject&, const core::CallContext&, const core::CommandDone& done) {
                                     QTimer::singleShot(1500ms, [this, done] {
                                         ++slowAnswers;
                                         done(core::CommandOutcome{{{u"late"_s, true}}, nullptr});
                                     });
                                 });
    }

    int counted = 0;
    int slowAnswers = 0;
    std::vector<QJsonObject> recorded;
};

}  // namespace

TEST_CASE("A Lua plugin's command runs its handler and answers with its result")
{
    LuaFixture f;
    REQUIRE(f.activate(R"lua(
        ms.command{
            id = "test.plugin.greet",
            title = "Greet",
            params = { type = "object", properties = { name = { type = "string", default = "world" } } },
            read_only = true,
            handler = function(params, ctx)
                return { message = ms.settings().greeting .. ", " .. params.name .. "!", origin = ctx.origin }
            end,
        }
    )lua"));
    REQUIRE(f.bus.contains(u"test.plugin.greet"_s));
    CHECK(f.bus.spec(u"test.plugin.greet"_s)->owner == u"test.plugin"_s);
    CHECK(f.bus.spec(u"test.plugin.greet"_s)->category == u"Test plugin"_s);

    const auto answer = f.run(u"test.plugin.greet"_s, {{u"name"_s, u"Ada"_s}});
    REQUIRE(answer);
    CHECK(answer->value(u"message"_s).toString() == u"Hi, Ada!"_s);
    CHECK(answer->value(u"origin"_s).toString() == u"api"_s);

    // Settings changes reach the plugin's next call.
    f.plugin->setSettings({{u"greeting"_s, u"Hello"_s}});
    CHECK(f.run(u"test.plugin.greet"_s)->value(u"message"_s).toString() == u"Hello, world!"_s);
}

TEST_CASE("A command id outside the plugin's own is refused when it is registered")
{
    LuaFixture f;
    REQUIRE_FALSE(f.activate(R"lua(
        ms.command{ id = "other.greet", title = "Greet", handler = function() return {} end }
    )lua"));
    CHECK(f.failure.contains(u"must start with the plugin's id"_s));
    CHECK_FALSE(f.bus.contains(u"other.greet"_s));
}

TEST_CASE("The sandbox has no files, processes, C modules or bytecode")
{
    LuaFixture f;
    f.write(u"helpers/text.lua"_s, "return { shout = function(s) return s:upper() end }");
    REQUIRE(f.activate(R"lua(
        local function refused(fn) return not pcall(fn) end
        ms.command{
            id = "test.plugin.probe",
            title = "Probe",
            handler = function()
                return {
                    io = io == nil,
                    execute = os.execute == nil,
                    getenv = os.getenv == nil,
                    remove = os.remove == nil,
                    package = package == nil,
                    dump = string.dump == nil,
                    dofile = dofile == nil,
                    loadfile = loadfile == nil,
                    debug = debug == nil,
                    binary = load(string.char(27) .. "Lua") == nil,
                    text = load("return 1 + 1")() == 2,
                    module = require("helpers.text").shout("hi") == "HI",
                    outside = refused(function() require("..main") end),
                    c_module = refused(function() require("socket.core") end),
                    time = type(os.time()) == "number",
                }
            end,
        }
    )lua"));
    const auto answer = f.run(u"test.plugin.probe"_s);
    REQUIRE(answer);
    for (auto it = answer->constBegin(); it != answer->constEnd(); ++it) {
        INFO(it.key().toStdString());
        CHECK(it.value().toBool());
    }
    CHECK(answer->size() == 15);
}

TEST_CASE("A plugin's call without the permission is refused with permission_denied")
{
    LuaFixture f;
    TestCommands commands(f.bus);
    REQUIRE(f.activate(R"lua(
        ms.command{
            id = "test.plugin.try",
            title = "Try",
            handler = function()
                local ok, problem = pcall(ms.run, "test.count")
                return { ok = ok, code = problem.code, message = problem.message }
            end,
        }
    )lua",
                       {u"project.read"_s}));
    const auto answer = f.run(u"test.plugin.try"_s);
    REQUIRE(answer);
    CHECK_FALSE(answer->value(u"ok"_s).toBool());
    CHECK(answer->value(u"code"_s).toString() == u"permission_denied"_s);
    CHECK(commands.counted == 0);
}

TEST_CASE("A plugin can't agree for the person: needs_confirmation reaches Lua")
{
    LuaFixture f;
    TestCommands commands(f.bus);
    REQUIRE(f.activate(R"lua(
        ms.command{
            id = "test.plugin.try",
            title = "Try",
            handler = function()
                local _, replace = pcall(ms.run, "test.replace", { overwrite = true })
                local _, ask = pcall(ms.run, "test.ask")
                return { replace = replace.code, question = replace.message, ask = ask.code }
            end,
        }
    )lua"));
    const auto answer = f.run(u"test.plugin.try"_s);
    REQUIRE(answer);
    CHECK(answer->value(u"replace"_s).toString() == u"needs_confirmation"_s);
    CHECK(answer->value(u"question"_s).toString() == u"Replace it?"_s);
    CHECK(answer->value(u"ask"_s).toString() == u"needs_confirmation"_s);
}

TEST_CASE("A Lua error fails the run with its message, and its traceback goes to the log")
{
    LogCapture log;
    LuaFixture f;
    REQUIRE(f.activate(R"lua(
        local function inner() error("the clip is gone") end
        ms.command{ id = "test.plugin.break", title = "Break", handler = function() inner() end }
    )lua"));
    const auto answer = f.run(u"test.plugin.break"_s);
    REQUIRE_FALSE(answer);
    CHECK(answer.error().code == CommandErrorCode::Failed);
    CHECK(answer.error().message.contains(u"the clip is gone"_s));
    CHECK(log.text().contains(u"stack traceback"_s));
    CHECK(log.text().contains(u"inner"_s));
}

TEST_CASE("Code that runs too long without waiting is stopped, and the main thread never waits for it")
{
    LuaFixture f;
    REQUIRE(f.activate(R"lua(
        ms.command{ id = "test.plugin.spin", title = "Spin", handler = function() while true do end end }
        ms.command{ id = "test.plugin.ok", title = "OK", handler = function() return { ok = true } end }
    )lua"));
    QElapsedTimer timer;
    timer.start();
    std::optional<std::expected<QJsonObject, core::CommandError>> answer;
    f.bus.executeAsync(
        u"test.plugin.spin"_s, {}, core::CallContext::api(), &f.bus,
        [&answer](std::expected<QJsonObject, core::CommandError> result) { answer = std::move(result); });
    CHECK(timer.elapsed() < 100);  // The run went to the plugin's thread.
    int ticks = 0;
    QTimer ticker;
    QObject::connect(&ticker, &QTimer::timeout, [&ticks] { ++ticks; });
    ticker.start(20ms);
    REQUIRE(waitFor([&answer] { return answer.has_value(); }));
    CHECK(ticks >= 10);  // The main thread's event loop kept running meanwhile.
    REQUIRE_FALSE(*answer);
    CHECK(answer->error().message.contains(u"without waiting"_s));
    // The state lives on.
    CHECK(f.run(u"test.plugin.ok"_s)->value(u"ok"_s).toBool());
}

TEST_CASE("A state that needs more memory than its limit gets an error instead of the memory")
{
    LuaFixture f;
    f.options.runLimit = 10s;  // Filling the memory takes longer than the usual limit on a busy machine.
    REQUIRE(f.activate(R"lua(
        ms.command{
            id = "test.plugin.grow",
            title = "Grow",
            handler = function()
                local t = {}
                for i = 1, 1e8 do t[i] = string.rep("x", 100) .. i end
            end,
        }
    )lua"));
    const auto answer = f.run(u"test.plugin.grow"_s);
    REQUIRE_FALSE(answer);
    CAPTURE(answer.error().message);
    CHECK(answer.error().message.contains(u"not enough memory"_s));
}

TEST_CASE("ms.run works only in the plugin's tasks, not in a coroutine of its own")
{
    LuaFixture f;
    TestCommands commands(f.bus);
    REQUIRE(f.activate(R"lua(
        ms.command{
            id = "test.plugin.nest",
            title = "Nest",
            handler = function()
                local co = coroutine.create(function() return ms.run("test.count") end)
                local ok, problem = coroutine.resume(co)
                return { ok = ok, problem = tostring(problem) }
            end,
        }
    )lua"));
    const auto answer = f.run(u"test.plugin.nest"_s);
    REQUIRE(answer);
    CHECK_FALSE(answer->value(u"ok"_s).toBool());
    CHECK(answer->value(u"problem"_s).toString().contains(u"works only in"_s));
    CHECK(commands.counted == 0);
}

TEST_CASE("A handler awaits a job without holding its thread")
{
    LuaFixture f;
    f.jobs.registerKind(
        u"test.wait"_s,
        core::JobKind{.permission = u"project.read"_s,
                      .start = [](core::JobManager& jobs, core::JobSpec spec, const QJsonObject& input) {
                          spec.title = u"Wait"_s;
                          return jobs.submit(std::move(spec), [&jobs, input](const QString& id) {
                              QTimer::singleShot(100ms, &jobs, [&jobs, id, input] { jobs.succeed(id, input); });
                          });
                      }});
    REQUIRE(f.activate(R"lua(
        ms.command{
            id = "test.plugin.wait",
            title = "Wait",
            handler = function()
                local job = ms.jobs.start("test.wait", { value = 7 })
                return { value = job:await().value, id = job.id }
            end,
        }
        ms.command{ id = "test.plugin.ok", title = "OK", handler = function() return { ok = true } end }
    )lua"));
    std::optional<std::expected<QJsonObject, core::CommandError>> waited;
    f.bus.executeAsync(
        u"test.plugin.wait"_s, {}, core::CallContext::api(), &f.bus,
        [&waited](std::expected<QJsonObject, core::CommandError> result) { waited = std::move(result); });
    // While the job runs, the plugin answers other calls.
    CHECK(f.run(u"test.plugin.ok"_s)->value(u"ok"_s).toBool());
    CHECK_FALSE(waited.has_value());
    REQUIRE(waitFor([&waited] { return waited.has_value(); }));
    REQUIRE(*waited);
    CHECK((*waited)->value(u"value"_s).toInt() == 7);
    const QString jobId = (*waited)->value(u"id"_s).toString();
    const QJsonArray list = f.jobs.jobsJson();
    REQUIRE(list.size() == 1);
    CHECK(list.first().toObject().value(u"owner"_s).toString() == u"test.plugin"_s);
    CHECK(list.first().toObject().value(u"origins"_s).toArray().last().toString() == u"plugin:test.plugin"_s);
    CHECK(list.first().toObject().value(u"id"_s).toString() == jobId);
}

TEST_CASE("A job kind the plugin lacks the permission for is refused")
{
    LuaFixture f;
    f.jobs.registerKind(u"test.net"_s,
                        core::JobKind{.permission = u"network"_s,
                                      .start = [](core::JobManager& jobs, core::JobSpec spec, const QJsonObject&) {
                                          return jobs.submit(std::move(spec), [](const QString&) { });
                                      }});
    REQUIRE(f.activate(R"lua(
        ms.command{
            id = "test.plugin.net",
            title = "Net",
            handler = function()
                local _, problem = pcall(ms.jobs.start, "test.net", {})
                local _, unknown = pcall(ms.jobs.start, "test.nothing", {})
                return { code = problem.code, unknown = unknown.code }
            end,
        }
    )lua",
                       {u"project.read"_s}));
    const auto answer = f.run(u"test.plugin.net"_s);
    REQUIRE(answer);
    CHECK(answer->value(u"code"_s).toString() == u"permission_denied"_s);
    CHECK(answer->value(u"unknown"_s).toString() == u"invalid_params"_s);
    CHECK(f.jobs.jobsJson().isEmpty());
}

TEST_CASE("ms.transaction makes the plugin's changes one undo step, and an error undoes them")
{
    LuaFixture f;
    TestCommands commands(f.bus);
    REQUIRE(f.activate(R"lua(
        ms.command{
            id = "test.plugin.twice",
            title = "Twice",
            params = { type = "object", properties = { fail = { type = "boolean" } } },
            handler = function(params)
                ms.transaction("Count twice", function()
                    ms.run("test.count")
                    ms.run("test.count")
                    if params.fail then error("stop here") end
                end)
                return {}
            end,
        }
    )lua"));
    REQUIRE(f.run(u"test.plugin.twice"_s));
    CHECK(commands.counted == 2);
    CHECK(f.undo.undoText() == u"Count twice"_s);
    CHECK(f.undo.undoOrigins() == QStringList{u"api"_s, u"plugin:test.plugin"_s});

    const auto failed = f.run(u"test.plugin.twice"_s, {{u"fail"_s, true}});
    REQUIRE_FALSE(failed);
    CHECK(failed.error().message.contains(u"stop here"_s));
    CHECK(commands.counted == 2);
    CHECK_FALSE(f.bus.inTransaction());

    // One step undoes both changes.
    REQUIRE(f.undo.undo());
    CHECK(commands.counted == 0);
    CHECK_FALSE(f.undo.canUndo());
}

TEST_CASE("ms.on hands the plugin the events it subscribed to")
{
    LuaFixture f;
    TestCommands commands(f.bus);
    REQUIRE(f.activate(R"lua(
        ms.on("test.ping", function(payload, topic)
            ms.run("test.record", { value = payload.value, topic = topic })
        end)
        ms.on("other.*", function(payload, topic)
            ms.run("test.record", { value = payload.value, topic = topic })
        end)
    )lua"));
    f.bus.publish(u"test.ping"_s, {{u"value"_s, 1}});
    f.bus.publish(u"test.unrelated"_s, {{u"value"_s, 2}});
    f.bus.publish(u"other.thing"_s, {{u"value"_s, 3}});
    REQUIRE(waitFor([&commands] { return commands.recorded.size() == 2; }));
    CHECK(commands.recorded[0].value(u"topic"_s).toString() == u"test.ping"_s);
    CHECK(commands.recorded[1].value(u"value"_s).toInt() == 3);
}

TEST_CASE("ms.project reads the project as it is, read-only")
{
    LuaFixture f;
    REQUIRE(f.project.addTrack(core::TrackKind::Video));
    REQUIRE(f.activate(R"lua(
        ms.command{
            id = "test.plugin.look",
            title = "Look",
            handler = function()
                local project = ms.project()
                local first = project.tracks[1]
                return {
                    revision = project.revision,
                    tracks = #project.tracks,
                    found = project:find_track(first.id).id == first.id,
                    frozen = not pcall(function() project.tracks = {} end),
                    deep = not pcall(function() first.name = "x" end),
                    clips = #project:clips_in_range(0, 100),
                }
            end,
        }
    )lua"));
    const auto answer = f.run(u"test.plugin.look"_s);
    REQUIRE(answer);
    CHECK(answer->value(u"revision"_s).toInteger() == static_cast<qint64>(f.project.snapshot()->revision));
    CHECK(answer->value(u"tracks"_s).toInt() == f.project.tracks().size());
    CHECK(answer->value(u"found"_s).toBool());
    CHECK(answer->value(u"frozen"_s).toBool());
    CHECK(answer->value(u"deep"_s).toBool());
    CHECK(answer->value(u"clips"_s).toInt() == 0);
}

TEST_CASE("Hot reload runs the changed code and registers its commands again")
{
    LuaFixture f;
    f.options.hotReload = true;
    REQUIRE(f.activate(R"lua(
        ms.command{ id = "test.plugin.say", title = "One", handler = function() return { said = "one" } end }
    )lua"));
    CHECK(f.run(u"test.plugin.say"_s)->value(u"said"_s).toString() == u"one"_s);

    f.write(u"main.lua"_s, R"lua(
        ms.command{ id = "test.plugin.say", title = "Two", handler = function() return { said = "two" } end }
    )lua");
    REQUIRE(waitFor([&f] { return f.starts == 2; }));
    REQUIRE(f.bus.contains(u"test.plugin.say"_s));
    CHECK(f.bus.spec(u"test.plugin.say"_s)->title == u"Two"_s);
    CHECK(f.run(u"test.plugin.say"_s)->value(u"said"_s).toString() == u"two"_s);
}

TEST_CASE("A reload cancels the runs in flight and drops the answers that arrive late")
{
    LuaFixture f;
    f.options.hotReload = true;
    TestCommands commands(f.bus);
    REQUIRE(f.activate(R"lua(
        ms.command{
            id = "test.plugin.slow",
            title = "Slow",
            handler = function()
                ms.run("test.slow")
                ms.run("test.count")
                return {}
            end,
        }
    )lua"));
    std::optional<std::expected<QJsonObject, core::CommandError>> answer;
    f.bus.executeAsync(
        u"test.plugin.slow"_s, {}, core::CallContext::api(), &f.bus,
        [&answer](std::expected<QJsonObject, core::CommandError> result) { answer = std::move(result); });
    f.write(u"main.lua"_s, R"lua(
        ms.command{ id = "test.plugin.slow", title = "Slow", handler = function() return { fast = true } end }
    )lua");
    REQUIRE(waitFor([&answer] { return answer.has_value(); }));
    REQUIRE_FALSE(*answer);
    CHECK(answer->error().code == CommandErrorCode::Canceled);
    REQUIRE(waitFor([&f] { return f.starts == 2; }));
    CHECK(commands.slowAnswers == 0);
    // test.slow answers the old state's call after the reload: nothing runs on.
    REQUIRE(waitFor([&commands] { return commands.slowAnswers == 1; }));
    QCoreApplication::processEvents();
    CHECK(commands.counted == 0);
    CHECK(f.run(u"test.plugin.slow"_s)->value(u"fast"_s).toBool());
}
