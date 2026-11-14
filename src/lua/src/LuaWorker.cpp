// A Lua plugin's state on its worker thread: the sandbox, the memory and run limits, the host
// functions the prelude calls and the prelude's entry points the plugin calls. The only file that
// uses sol2.
#include "LuaWorker.h"

#include "CommandSpecs.h"
#include "Logging.h"
#include "LuaJson.h"
#include "LuaPlugin.h"
#include "Prelude.h"
#include "mikserstudio/core/ProjectSnapshot.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QThread>

#include <sol/sol.hpp>

#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>

using namespace Qt::StringLiterals;

namespace mikserstudio::lua {

namespace {

// Plugin code is checked against its limits every this many instructions.
constexpr int kHookInstructions = 1000;

// Counts the bytes a state holds and refuses to grow past the cap.
struct Allocator {
    std::size_t used = 0;
    std::size_t cap = 0;
};

void* allocate(void* userData, void* block, std::size_t oldSize, std::size_t newSize)
{
    auto* allocator = static_cast<Allocator*>(userData);
    const std::size_t old = block ? oldSize : 0;
    if (newSize == 0) {
        std::free(block);  // NOLINT(cppcoreguidelines-no-malloc): Lua's allocator contract.
        allocator->used -= old;
        return nullptr;
    }
    if (newSize > old && allocator->used - old + newSize > allocator->cap)
        return nullptr;
    void* grown = std::realloc(block, newSize);  // NOLINT(cppcoreguidelines-no-malloc)
    if (grown)
        allocator->used = allocator->used - old + newSize;
    return grown;
}

// What the count hook checks, reached through the state's extra space (each coroutine starts
// with a copy of it).
struct Limits {
    std::chrono::steady_clock::time_point deadline;
    std::chrono::milliseconds runLimit{0};
    const std::atomic<bool>* stopping = nullptr;
};

void countHook(lua_State* state, lua_Debug* /*debug*/)
{
    const Limits* limits = *static_cast<Limits**>(lua_getextraspace(state));
    if (limits->stopping->load(std::memory_order_relaxed)) {
        lua_pushstring(state, "the plugin is stopping");
        lua_error(state);
    }
    if (std::chrono::steady_clock::now() > limits->deadline) {
        const QByteArray message =
            u"the plugin's code ran for more than %1 s without waiting; split the work or run it as a job"_s
                .arg(std::chrono::duration<double>(limits->runLimit).count())
                .toUtf8();
        lua_pushlstring(state, message.constData(), static_cast<std::size_t>(message.size()));
        lua_error(state);
    }
}

QString text(const std::string& value)
{
    return QString::fromStdString(value);
}

sol::object toLua(lua_State* state, const QJsonValue& value)
{
    pushJson(state, value);
    return sol::stack::pop<sol::object>(state);
}

std::expected<QJsonValue, QString> fromLua(lua_State* state, const sol::object& value)
{
    value.push(state);
    auto json = toJson(state, -1);
    lua_pop(state, 1);
    return json;
}

std::expected<QJsonObject, QString> objectFromLua(lua_State* state, const sol::object& value, const QString& what)
{
    auto json = fromLua(state, value);
    if (!json)
        return std::unexpected(u"%1 can't be sent: %2"_s.arg(what, json.error()));
    if (json->isNull())
        return QJsonObject();
    if (!json->isObject())
        return std::unexpected(u"%1 must be a table with string keys"_s.arg(what));
    return json->toObject();
}

// A module name as require() takes it: lower-case words joined by dots.
const QRegularExpression& moduleName()
{
    static const QRegularExpression name(u"^[a-z_][a-z0-9_]*(\\.[a-z_][a-z0-9_]*)*$"_s);
    return name;
}

using Sent = std::tuple<bool, std::string>;

Sent sent()
{
    return {true, {}};
}

Sent notSent(const QString& problem)
{
    return {false, problem.toStdString()};
}

}  // namespace

struct LuaWorker::State {
    State(WorkerConfig workerConfig, std::shared_ptr<WorkerShared> workerShared, std::shared_ptr<MainLink> mainLink)
        : config(std::move(workerConfig))
        , shared(std::move(workerShared))
        , link(std::move(mainLink))
    {
        allocator.cap = config.memoryLimit;
        limits.runLimit = config.runLimit;
        limits.stopping = &shared->stopping;
    }

    void post(std::function<void(LuaPlugin&)> call) const { link->post(std::move(call)); }
    void fail(const QString& message) const;
    [[nodiscard]] bool setUp();
    void addHost(sol::table& host);

    // Calls one of the prelude's entry points with a fresh run limit.
    template <typename... Args> void call(const sol::protected_function& entry, Args&&... args)
    {
        limits.deadline = std::chrono::steady_clock::now() + config.runLimit;
        const sol::protected_function_result result = entry(std::forward<Args>(args)...);
        if (!result.valid()) {
            const sol::error error = result;
            qCWarning(lcLua).noquote() << u"[%1]"_s.arg(config.pluginId) << error.what();
        }
    }

    WorkerConfig config;
    std::shared_ptr<WorkerShared> shared;
    std::shared_ptr<MainLink> link;
    Allocator allocator;
    Limits limits;
    std::size_t reportedDrops = 0;

    // Declared before the references into it, so they go first.
    std::unique_ptr<sol::state> lua;
    sol::protected_function invoke;
    sol::protected_function answer;
    sol::protected_function event;
    sol::protected_function main;
};

void LuaWorker::State::fail(const QString& message) const
{
    post([generation = config.generation, message](LuaPlugin& plugin) { plugin.started(generation, false, message); });
}

void LuaWorker::State::addHost(sol::table& host)
{
    lua_State* state = lua->lua_state();
    const quint64 generation = config.generation;
    host["plugin_id"] = config.pluginId.toStdString();
    lua_pushlightuserdata(state, nullptr);
    host["null"] = sol::stack::pop<sol::object>(state);
    luaL_getmetatable(state, kArrayMetatable);
    host["array_mt"] = sol::stack::pop<sol::object>(state);

    host.set_function("register",
                      [this, generation](sol::this_state ts, const sol::object& fields, std::int64_t handler) -> Sent {
                          auto spec = objectFromLua(ts, fields, u"The command's spec"_s);
                          if (!spec)
                              return notSent(spec.error());
                          auto command = commandSpecFromJson(*spec, config.pluginId, config.pluginName);
                          if (!command)
                              return notSent(command.error());
                          post([generation, command = std::move(*command), handler](LuaPlugin& plugin) mutable {
                              plugin.registerCommand(generation, std::move(command), handler);
                          });
                          return sent();
                      });
    host.set_function("run",
                      [this, generation](sol::this_state ts, std::int64_t call, std::int64_t run, const std::string& id,
                                         const sol::object& params) -> Sent {
                          auto json = objectFromLua(ts, params, u"The params"_s);
                          if (!json)
                              return notSent(json.error());
                          post([generation, call, run, id = text(id), params = *json](LuaPlugin& plugin) {
                              plugin.run(generation, call, run, id, params);
                          });
                          return sent();
                      });
    host.set_function(
        "done", [this, generation](sol::this_state ts, std::int64_t run, bool ok, const sol::object& value) {
            auto json = objectFromLua(ts, value, u"The command's result"_s);
            if (!json) {
                ok = false;
                json = QJsonObject{{u"code"_s, u"failed"_s}, {u"message"_s, json.error()}};
            }
            post([generation, run, ok, value = *json](LuaPlugin& plugin) { plugin.done(generation, run, ok, value); });
        });
    host.set_function("subscribe", [this, generation](const std::string& topic) {
        post([generation, topic = text(topic)](LuaPlugin& plugin) { plugin.subscribe(generation, topic); });
    });
    host.set_function("begin",
                      [this, generation](std::int64_t call, std::int64_t run, const std::string& name) -> Sent {
                          post([generation, call, run, name = text(name)](LuaPlugin& plugin) {
                              plugin.beginTransaction(generation, call, run, name);
                          });
                          return sent();
                      });
    host.set_function("finish",
                      [this, generation](std::int64_t call, std::int64_t transaction, bool commit,
                                         const sol::optional<std::string>& reason) -> Sent {
                          post([generation, call, transaction, commit,
                                reason = text(reason.value_or(std::string()))](LuaPlugin& plugin) {
                              plugin.finishTransaction(generation, call, transaction, commit, reason);
                          });
                          return sent();
                      });
    host.set_function("start_job",
                      [this, generation](sol::this_state ts, std::int64_t call, std::int64_t run,
                                         const std::string& kind, const sol::object& input) -> Sent {
                          auto json = objectFromLua(ts, input, u"The job's input"_s);
                          if (!json)
                              return notSent(json.error());
                          post([generation, call, run, kind = text(kind), input = *json](LuaPlugin& plugin) {
                              plugin.startJob(generation, call, run, kind, input);
                          });
                          return sent();
                      });
    host.set_function("started", [this, generation](bool ok, const sol::optional<std::string>& message) {
        post([generation, ok, message = text(message.value_or(std::string()))](LuaPlugin& plugin) {
            plugin.started(generation, ok, message);
        });
    });
    host.set_function("log", [this](const std::string& level, const std::string& message) {
        const QString line = u"[%1] %2"_s.arg(config.pluginId, text(message));
        if (level == "debug")
            qCDebug(lcLua).noquote() << line;
        else if (level == "info")
            qCInfo(lcLua).noquote() << line;
        else if (level == "warning")
            qCWarning(lcLua).noquote() << line;
        else
            qCCritical(lcLua).noquote() << line;
    });
    host.set_function("settings", [this](sol::this_state ts) {
        QJsonObject settings;
        {
            const std::scoped_lock lock(shared->mutex);
            settings = shared->settings;
        }
        return toLua(ts, settings);
    });
    host.set_function("revision", [this] -> std::tuple<std::string, std::int64_t> {
        const auto snapshot = config.snapshot();
        return {snapshot->session.toStdString(), static_cast<std::int64_t>(snapshot->revision)};
    });
    host.set_function("snapshot", [this](sol::this_state ts) { return toLua(ts, config.snapshot()->toJson()); });
    host.set_function(
        "read_module", [this](const std::string& name) -> std::tuple<std::optional<std::string>, std::string> {
            const QString module = text(name);
            if (!moduleName().match(module).hasMatch())
                return {std::nullopt, u"“%1” is not a module name: use lower-case words joined by "
                                      "dots, such as “helpers.text”"_s.arg(module)
                                          .toStdString()};
            const QString relative = QString(module).replace(u'.', u'/') + u".lua"_s;
            QFile file(QDir(config.directory).filePath(relative));
            if (!file.open(QIODevice::ReadOnly))
                return {std::nullopt,
                        u"module “%1” not found: there is no %2 in the plugin's folder"_s.arg(module, relative)
                            .toStdString()};
            return {file.readAll().toStdString(), std::string()};
        });
}

bool LuaWorker::State::setUp()
{
    lua = std::make_unique<sol::state>(sol::default_at_panic, &allocate, &allocator);
    lua_State* state = lua->lua_state();
    // Before any code runs: every coroutine inherits the hook and the extra space.
    *static_cast<Limits**>(lua_getextraspace(state)) = &limits;
    lua_sethook(state, &countHook, LUA_MASKCOUNT, kHookInstructions);
    limits.deadline = std::chrono::steady_clock::now() + config.runLimit;

    lua->open_libraries(sol::lib::base, sol::lib::string, sol::lib::table, sol::lib::math, sol::lib::utf8,
                        sol::lib::coroutine, sol::lib::os, sol::lib::debug);
    luaL_newmetatable(state, kArrayMetatable);
    lua_pop(state, 1);

    // The sandbox: no files, processes or bytecode. The prelude defines require and a text-only
    // load; debug.traceback goes only to the prelude.
    sol::table os = lua->get<sol::table>("os");
    sol::table safeOs = lua->create_table();
    for (const char* name : {"time", "clock", "date"})
        safeOs[name] = os.get<sol::object>(name);
    (*lua)["os"] = safeOs;
    (*lua)["dofile"] = sol::lua_nil;
    (*lua)["loadfile"] = sol::lua_nil;
    lua->get<sol::table>("string")["dump"] = sol::lua_nil;
    const sol::object rawLoad = lua->get<sol::object>("load");
    const sol::object traceback = lua->get<sol::table>("debug").get<sol::object>("traceback");
    (*lua)["load"] = sol::lua_nil;
    (*lua)["debug"] = sol::lua_nil;

    sol::table host = lua->create_table();
    addHost(host);

    const sol::load_result prelude = lua->load(std::string_view(kPrelude), "=prelude", sol::load_mode::text);
    if (!prelude.valid()) {
        const sol::error error = prelude;
        fail(u"The Lua host's prelude doesn't load: %1. Report this problem."_s.arg(QString::fromUtf8(error.what())));
        return false;
    }
    const auto run = prelude.get<sol::protected_function>();
    const sol::protected_function_result result = run(host, traceback, rawLoad);
    if (!result.valid()) {
        const sol::error error = result;
        fail(u"The Lua host's prelude failed: %1. Report this problem."_s.arg(QString::fromUtf8(error.what())));
        return false;
    }
    const sol::table dispatch = result.get<sol::table>();
    invoke = dispatch.get<sol::protected_function>("invoke");
    answer = dispatch.get<sol::protected_function>("answer");
    event = dispatch.get<sol::protected_function>("event");
    main = dispatch.get<sol::protected_function>("main");
    return true;
}

LuaWorker::LuaWorker(WorkerConfig config, std::shared_ptr<WorkerShared> shared, std::shared_ptr<MainLink> link)
    : m_state(std::make_unique<State>(std::move(config), std::move(shared), std::move(link)))
{
}

LuaWorker::~LuaWorker() = default;

void LuaWorker::start()
{
    State& s = *m_state;
    if (!s.setUp())
        return;
    const QString path = QDir(s.config.directory).filePath(s.config.entry);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        s.fail(u"Can't read the plugin's entry file %1: %2."_s.arg(QDir::toNativeSeparators(path), file.errorString()));
        return;
    }
    const std::string source = file.readAll().toStdString();
    s.call(s.main, source, u"@%1"_s.arg(QFileInfo(path).fileName()).toStdString());
}

void LuaWorker::invoke(qint64 run, qint64 handler, const QJsonObject& params, const QJsonObject& context)
{
    State& s = *m_state;
    if (!s.lua)
        return;
    lua_State* state = s.lua->lua_state();
    s.call(s.invoke, run, handler, toLua(state, params), toLua(state, context));
}

void LuaWorker::answer(qint64 call, bool ok, const QJsonValue& value)
{
    State& s = *m_state;
    if (!s.lua)
        return;
    s.call(s.answer, call, ok, toLua(s.lua->lua_state(), value));
}

void LuaWorker::drainEvents()
{
    State& s = *m_state;
    s.shared->drainQueued = false;
    const std::vector<core::Event> events = s.shared->inbox.takeAll();
    if (!s.lua)
        return;
    lua_State* state = s.lua->lua_state();
    for (const core::Event& event : events)
        s.call(s.event, event.topic.toStdString(), toLua(state, event.payload));
    if (const std::size_t dropped = s.shared->inbox.dropped(); dropped > s.reportedDrops) {
        qCWarning(lcLua).noquote() << u"[%1]"_s.arg(s.config.pluginId) << dropped - s.reportedDrops
                                   << "events were dropped because the plugin reads them too slowly";
        s.reportedDrops = dropped;
    }
}

void LuaWorker::shutdown()
{
    State& s = *m_state;
    s.invoke = {};
    s.answer = {};
    s.event = {};
    s.main = {};
    s.lua = nullptr;
    QThread::currentThread()->quit();
}

}  // namespace mikserstudio::lua
