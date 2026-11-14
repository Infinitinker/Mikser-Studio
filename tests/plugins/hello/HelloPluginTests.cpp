// The example plugin, plugins/examples/hello: discovered from its manifest, started by the Lua host,
// greeting and following its settings.
#include "LuaTestSupport.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/JobManager.h"
#include "mikserstudio/core/PluginManager.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/core/ProjectSnapshot.h"
#include "mikserstudio/core/UndoStack.h"
#include "mikserstudio/lua/LuaRuntime.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <optional>

using namespace Qt::StringLiterals;
using namespace mikserstudio::core;
using mikserstudio::tests::waitFor;

namespace {

const QString kHello = u"mikserstudio.examples.hello"_s;
const QString kGreet = u"mikserstudio.examples.hello.greet"_s;

QJsonObject json(const char* text)
{
    return QJsonDocument::fromJson(text).object();
}

struct Fixture {
    Fixture() { manager.setRuntime(&runtime); }

    // Runs the greet command as the API would.
    std::expected<QJsonObject, CommandError> greet(const QJsonObject& params = {})
    {
        std::optional<std::expected<QJsonObject, CommandError>> answer;
        bus.executeAsync(kGreet, params, CallContext::api(), &bus,
                         [&answer](std::expected<QJsonObject, CommandError> result) { answer = std::move(result); });
        REQUIRE(waitFor([&answer] { return answer.has_value(); }));
        return *answer;
    }

    int argc = 0;
    QCoreApplication app{argc, nullptr};
    QTemporaryDir settings;
    UndoStack undo;
    CommandBus bus{undo};
    JobManager jobs{bus};
    ProjectModel project;
    mikserstudio::lua::LuaRuntime runtime{
        {.bus = &bus, .jobs = &jobs, .snapshot = [this] { return project.snapshot(); }, .hotReload = false}};
    PluginManager manager{bus, settings.path()};
};

}  // namespace

TEST_CASE("The example plugin is discovered, greets and follows its settings", "[plugins][hello]")
{
    Fixture f;
    f.manager.addSearchPath(QString::fromUtf8(MIKSERSTUDIO_EXAMPLE_PLUGINS), PluginSource::Development);
    f.manager.discover();

    const PluginState* hello = f.manager.plugin(kHello);
    REQUIRE(hello);
    CHECK(hello->status == PluginStatus::Discovered);
    CHECK(hello->manifest.name == u"Hello"_s);
    CHECK(hello->granted == QStringList{u"project.read"_s, u"ui"_s});
    REQUIRE(hello->manifest.panels.size() == 1);
    const PanelContribution& panel = hello->manifest.panels[0];
    CHECK(panel.id == u"greeter"_s);
    CHECK(panel.title == u"Greeter"_s);
    CHECK(panel.qml == u"hello.qml"_s);
    CHECK(panel.defaultArea == u"right"_s);
    CHECK(QFile::exists(hello->manifest.directory + u'/' + panel.qml));
    CHECK(f.manager.settings(kHello) == json(R"({"greeting": "Hello"})"));

    f.manager.activateAll();
    REQUIRE(waitFor([&f] { return f.manager.ready(); }));
    REQUIRE(f.manager.plugin(kHello)->status == PluginStatus::Active);
    const CommandSpec* greet = f.bus.spec(kGreet);
    REQUIRE(greet);
    CHECK(greet->title == u"Say hello"_s);
    CHECK(greet->category == u"Examples"_s);
    CHECK(greet->owner == kHello);
    CHECK(greet->readOnly);

    auto result = f.greet();
    REQUIRE(result);
    CHECK(*result == json(R"({"message": "Hello, world!"})"));

    result = f.greet(json(R"({"name": "Zoë \"Z\" \\ 😀"})"));
    REQUIRE(result);
    CHECK(result->value(u"message").toString() == u"Hello, Zoë \"Z\" \\ \U0001F600!"_s);

    REQUIRE(f.manager.setSettings(kHello, json(R"({"greeting": "Cześć"})")));
    result = f.greet(json(R"({"name": "Mikser Studio"})"));
    REQUIRE(result);
    CHECK(result->value(u"message").toString() == u"Cześć, Mikser Studio!"_s);

    const auto invalid = f.greet(json(R"({"name": 3})"));
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().code == CommandErrorCode::InvalidParams);

    f.manager.deactivate(kHello);
    CHECK_FALSE(f.bus.contains(kGreet));
}
