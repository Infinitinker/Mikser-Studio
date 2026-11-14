// The plugins.* commands: listing, turning plugins on and off (asking before one of the person's own
// gets its permissions), settings, rescanning, the plugins.manage permission and plugins.changed.
#include "FakePluginRuntime.h"
#include "TestSupport.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/EventBus.h"
#include "mikserstudio/core/PluginCommands.h"
#include "mikserstudio/core/PluginManager.h"
#include "mikserstudio/core/UndoStack.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <vector>

using namespace Qt::StringLiterals;
using namespace mikserstudio::core;
using mikserstudio::tests::FakePluginRuntime;
using mikserstudio::tests::json;

namespace {

const QString kBuiltIn = u"mikserstudio.tests.builtin"_s;
const QString kMine = u"mikserstudio.tests.mine"_s;

struct Fixture {
    Fixture()
    {
        install(u"plugins"_s, kBuiltIn);
        install(u"user"_s, kMine);
        manager.setRuntime(&runtime);
        manager.addSearchPath(temp.filePath(u"plugins"_s));
        manager.addSearchPath(temp.filePath(u"user"_s), PluginSource::User);
        manager.discover();
        registerPluginCommands(bus, manager);
        QObject::connect(&bus.events(), &EventBus::published, &bus, [this](const Event& event) {
            if (event.topic == u"plugins.changed"_s)
                changes.push_back(event.payload);
        });
    }

    void install(const QString& root, const QString& id) const
    {
        const QString folder = temp.filePath(root + u'/' + id);
        REQUIRE(QDir().mkpath(folder));
        QFile manifest(folder + u"/mikserstudio-plugin.json"_s);
        REQUIRE(manifest.open(QIODevice::WriteOnly));
        QJsonObject json = mikserstudio::tests::json(R"({
            "name": "Plugin", "version": "1.0.0", "apiVersion": 1, "entry": "main.lua",
            "permissions": ["project.read", "network"],
            "settingsSchema": {"type": "object", "properties": {"level": {"type": "integer", "default": 1}},
                               "additionalProperties": false}
        })");
        json.insert(u"id"_s, id);
        manifest.write(QJsonDocument(json).toJson());
    }

    // The plugin's entry in plugins.list.
    [[nodiscard]] QJsonObject listed(const QString& id)
    {
        const auto list = bus.execute(u"plugins.list"_s);
        REQUIRE(list);
        for (const QJsonValue plugin : list->value(u"plugins").toArray()) {
            if (plugin.toObject().value(u"id").toString() == id)
                return plugin.toObject();
        }
        return {};
    }

    QTemporaryDir temp;
    UndoStack undo;
    CommandBus bus{undo};
    FakePluginRuntime runtime;
    PluginManager manager{bus, temp.filePath(u"settings"_s)};
    std::vector<QJsonObject> changes;
};

}  // namespace

TEST_CASE("plugins.list lists each plugin with its status, source, permissions and settings", "[plugins]")
{
    Fixture f;
    f.manager.activateAll();
    auto list = f.bus.execute(u"plugins.list"_s);
    REQUIRE(list);
    CHECK_FALSE(list->value(u"ready").toBool());
    CHECK(list->value(u"plugins").toArray().size() == 2);
    const QJsonObject builtIn = f.listed(kBuiltIn);
    CHECK(builtIn.value(u"status").toString() == u"starting"_s);
    CHECK(builtIn.value(u"source").toString() == u"builtin"_s);
    CHECK(builtIn.value(u"permissions").toArray() == QJsonArray{u"project.read"_s, u"network"_s});
    CHECK(builtIn.value(u"settings").toObject() == json(R"({"level": 1})"));
    CHECK(builtIn.value(u"settingsSchema").toObject().contains(u"properties"_s));
    CHECK(f.listed(kMine).value(u"status").toString() == u"disabled"_s);
    CHECK(f.listed(kMine).value(u"source").toString() == u"user"_s);

    f.runtime.startAll();
    CHECK(f.listed(kBuiltIn).value(u"status").toString() == u"active"_s);
    list = f.bus.execute(u"plugins.list"_s);
    CHECK(list->value(u"ready").toBool());
    REQUIRE_FALSE(f.changes.empty());
    CHECK(f.changes.back() == json(R"({"ready": true})"));
}

TEST_CASE("plugins.set_enabled asks before one of the person's own plugins gets its permissions", "[plugins]")
{
    Fixture f;
    const auto asked = f.bus.execute(u"plugins.set_enabled"_s, {{u"id"_s, kMine}, {u"enabled"_s, true}},
                                     CallContext::api());
    REQUIRE_FALSE(asked);
    CHECK(asked.error().code == CommandErrorCode::NeedsConfirmation);
    CHECK(asked.error().message == u"Turn on “Plugin”? It gets these permissions: project.read, network."_s);
    CHECK(f.listed(kMine).value(u"status").toString() == u"disabled"_s);

    const auto granted = f.bus.execute(
        u"plugins.set_enabled"_s, {{u"id"_s, kMine}, {u"enabled"_s, true}, {u"grant"_s, true}}, CallContext::api());
    REQUIRE(granted);
    CHECK(granted->value(u"status").toString() == u"starting"_s);
    REQUIRE(f.runtime.last(kMine));
    CHECK(f.runtime.last(kMine)->running);

    const auto off = f.bus.execute(u"plugins.set_enabled"_s, {{u"id"_s, kMine}, {u"enabled"_s, false}});
    REQUIRE(off);
    CHECK(off->value(u"status").toString() == u"disabled"_s);
    CHECK_FALSE(f.runtime.last(kMine)->running);

    // A built-in plugin needs no agreement: it already has its permissions.
    REQUIRE(f.bus.execute(u"plugins.set_enabled"_s, {{u"id"_s, kBuiltIn}, {u"enabled"_s, false}}));
    const auto on = f.bus.execute(u"plugins.set_enabled"_s, {{u"id"_s, kBuiltIn}, {u"enabled"_s, true}},
                                  CallContext::api());
    REQUIRE(on);
    CHECK(on->value(u"status").toString() == u"starting"_s);

    const auto unknown = f.bus.execute(u"plugins.set_enabled"_s, {{u"id"_s, u"x.y"_s}, {u"enabled"_s, true}});
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().code == CommandErrorCode::InvalidParams);
}

TEST_CASE("plugins.set_settings validates and keeps the settings", "[plugins]")
{
    Fixture f;
    const auto set = f.bus.execute(u"plugins.set_settings"_s,
                                   {{u"id"_s, kBuiltIn}, {u"settings"_s, json(R"({"level": 3})")}});
    REQUIRE(set);
    CHECK(set->value(u"settings").toObject() == json(R"({"level": 3})"));
    CHECK(f.manager.settings(kBuiltIn) == json(R"({"level": 3})"));

    const auto wrong = f.bus.execute(u"plugins.set_settings"_s,
                                     {{u"id"_s, kBuiltIn}, {u"settings"_s, json(R"({"level": "high"})")}});
    REQUIRE_FALSE(wrong);
    CHECK(wrong.error().code == CommandErrorCode::InvalidParams);
    CHECK(wrong.error().message.contains(u"/level"_s));
}

TEST_CASE("plugins.rescan finds new plugins and starts the ones that are on", "[plugins]")
{
    Fixture f;
    f.manager.activateAll();
    f.runtime.startAll();
    f.install(u"plugins"_s, u"mikserstudio.tests.later"_s);
    const auto rescanned = f.bus.execute(u"plugins.rescan"_s);
    REQUIRE(rescanned);
    CHECK(rescanned->value(u"plugins").toArray().size() == 3);
    CHECK(f.listed(u"mikserstudio.tests.later"_s).value(u"status").toString() == u"starting"_s);
    CHECK(f.listed(kBuiltIn).value(u"status").toString() == u"active"_s);
    CHECK(f.listed(kMine).value(u"status").toString() == u"disabled"_s);
    CHECK(f.runtime.starts.size() == 2);
}

TEST_CASE("The plugins.* commands need plugins.manage", "[plugins]")
{
    Fixture f;
    const CallContext plugin{.origins = {u"plugin:other.plugin"_s}, .permissions = {u"project.read"_s}};
    for (const QString& id : {u"plugins.list"_s, u"plugins.rescan"_s}) {
        const auto refused = f.bus.execute(id, {}, plugin);
        REQUIRE_FALSE(refused);
        CHECK(refused.error().code == CommandErrorCode::PermissionDenied);
    }
}

TEST_CASE("A plugin can't turn itself off through its own call", "[plugins]")
{
    Fixture f;
    const CallContext self{.origins = {u"plugin:"_s + kBuiltIn}, .permissions = {u"plugins.manage"_s}};
    const auto refused = f.bus.execute(u"plugins.set_enabled"_s, {{u"id"_s, kBuiltIn}, {u"enabled"_s, false}}, self);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().code == CommandErrorCode::InvalidParams);
    CHECK(f.listed(kBuiltIn).value(u"status").toString() == u"discovered"_s);
}
