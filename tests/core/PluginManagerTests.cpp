// The plugin manager: discovery of valid and broken plugins, starting and stopping them through a
// plugin runtime, built-in and the person's own plugins and their permissions, and settings that are
// validated, saved and kept across restarts.
#include "TestSupport.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/PluginManager.h"
#include "mikserstudio/core/PluginRuntime.h"
#include "mikserstudio/core/UndoStack.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <algorithm>
#include <memory>
#include <ranges>

using namespace Qt::StringLiterals;
using namespace mikserstudio::core;
using mikserstudio::tests::json;

namespace {

const QString kFixtureId = u"mikserstudio.tests.fixture"_s;

QJsonObject fixtureManifest(const QString& id = kFixtureId)
{
    QJsonObject manifest = json(R"({
        "name": "Fixture",
        "version": "1.0.0",
        "apiVersion": 1,
        "entry": "main.lua",
        "permissions": ["project.read", "plugins.manage"],
        "settingsSchema": {
            "type": "object",
            "properties": {
                "mode": {"enum": ["quiet", "loud"], "default": "quiet"},
                "greeting": {"type": "string", "minLength": 1, "default": "hi"}
            },
            "additionalProperties": false
        }
    })");
    manifest.insert(u"id"_s, id);
    return manifest;
}

// One start of a plugin, as the fake runtime saw it.
struct Start {
    PluginManifest manifest;
    PluginActivation activation;
    bool running = true;
    QJsonObject settings;
};

class FakeInstance final : public PluginInstance {
public:
    explicit FakeInstance(std::shared_ptr<Start> start)
        : m_start(std::move(start))
    {
    }
    ~FakeInstance() override { m_start->running = false; }
    FakeInstance(const FakeInstance&) = delete;
    FakeInstance& operator=(const FakeInstance&) = delete;
    void setSettings(const QJsonObject& settings) override { m_start->settings = settings; }

private:
    std::shared_ptr<Start> m_start;
};

// A runtime that starts nothing by itself: the test says when a plugin has started or failed.
class FakeRuntime final : public PluginRuntime {
public:
    std::expected<std::unique_ptr<PluginInstance>, QString> activate(const PluginManifest& manifest,
                                                                     PluginActivation activation) override
    {
        if (!refusal.isEmpty())
            return std::unexpected(refusal);
        auto start = std::make_shared<Start>(
            Start{.manifest = manifest, .activation = std::move(activation), .running = true, .settings = {}});
        start->settings = start->activation.settings;
        starts.append(start);
        return std::make_unique<FakeInstance>(start);
    }

    // Says that every running plugin has started.
    void startAll() const
    {
        for (const auto& start : starts) {
            if (start->running)
                start->activation.started();
        }
    }

    [[nodiscard]] std::shared_ptr<Start> last(const QString& id) const
    {
        for (const auto& start : std::views::reverse(starts)) {
            if (start->manifest.id == id)
                return start;
        }
        return nullptr;
    }

    QString refusal;  // When set, activating fails at once with it.
    QList<std::shared_ptr<Start>> starts;
};

struct Environment {
    [[nodiscard]] QString pluginsDir(const QString& name = u"plugins"_s) const { return temp.filePath(name); }
    [[nodiscard]] QString settingsDir() const { return temp.filePath(u"settings"_s); }

    // Writes <root>/<folder>/ with the manifest and an entry file.
    void install(const QString& folder, const QJsonObject& manifest, const QString& root = u"plugins"_s) const
    {
        writeManifest(folder, QJsonDocument(manifest).toJson(), root);
        QFile entry(pluginsDir(root) + u'/' + folder + u"/main.lua"_s);
        REQUIRE(entry.open(QIODevice::WriteOnly));
    }

    void installFixture(const QString& folder = u"fixture"_s) const { install(folder, fixtureManifest()); }

    void writeManifest(const QString& folder, const QByteArray& content, const QString& root = u"plugins"_s) const
    {
        REQUIRE(QDir().mkpath(pluginsDir(root) + u'/' + folder));
        QFile file(pluginsDir(root) + u'/' + folder + u"/mikserstudio-plugin.json"_s);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write(content);
    }

    QTemporaryDir temp;
    UndoStack stack;
    CommandBus bus{stack};
    FakeRuntime runtime;
};

struct Manager {
    explicit Manager(Environment& env)
        : manager(env.bus, env.settingsDir())
    {
        manager.setRuntime(&env.runtime);
        manager.addSearchPath(env.pluginsDir());
        manager.addSearchPath(env.pluginsDir(u"user"_s), PluginSource::User);
        manager.addSearchPath(env.pluginsDir(u"development"_s), PluginSource::Development);
        QObject::connect(&manager, &PluginManager::pluginFailed,
                         [this](const QString& id, const QString& message) { failures.append({id, message}); });
        manager.discover();
    }

    [[nodiscard]] PluginStatus status(const QString& id) const
    {
        const PluginState* state = manager.plugin(id);
        REQUIRE(state);
        return state->status;
    }

    PluginManager manager;
    QList<std::pair<QString, QString>> failures;
};

}  // namespace

TEST_CASE("Discovery lists valid plugins and reports broken manifests", "[plugins]")
{
    Environment env;
    env.installFixture();
    env.writeManifest(u"broken_json"_s, "{ not json");
    env.writeManifest(u"bad_manifest"_s, R"({"id": "mikserstudio.tests.bad", "name": "Bad"})");
    REQUIRE(QDir().mkpath(env.pluginsDir() + u"/not_a_plugin"_s));

    Manager m(env);
    const QList<PluginState> plugins = m.manager.plugins();
    REQUIRE(plugins.size() == 3);
    CHECK(m.status(kFixtureId) == PluginStatus::Discovered);
    CHECK(m.status(u"broken_json"_s) == PluginStatus::Failed);
    CHECK(m.manager.plugin(u"broken_json"_s)->error.contains(u"not a valid JSON object"_s));
    // A broken manifest is listed under its folder name, not under the id it claims.
    CHECK(m.status(u"bad_manifest"_s) == PluginStatus::Failed);
    CHECK(m.manager.plugin(u"bad_manifest"_s)->error.contains(u"/version: is required"_s));
    CHECK(m.manager.plugin(u"mikserstudio.tests.bad"_s) == nullptr);
    CHECK(m.manager.plugin(kFixtureId)->manifest.directory ==
          QDir::fromNativeSeparators(env.pluginsDir() + u"/fixture"_s));

    REQUIRE(m.failures.size() == 2);
    QStringList failedIds{m.failures[0].first, m.failures[1].first};
    failedIds.sort();
    CHECK(failedIds == QStringList{u"bad_manifest"_s, u"broken_json"_s});

    CHECK_FALSE(m.manager.activate(u"bad_manifest"_s));
    CHECK_FALSE(m.manager.activate(u"mikserstudio.tests.unknown"_s));
    CHECK(env.runtime.starts.isEmpty());
}

TEST_CASE("A native or Python plugin is listed as failed, with the reason", "[plugins]")
{
    Environment env;
    env.writeManifest(u"python_plugin"_s, R"({"id": "mikserstudio.tests.py", "name": "Py", "version": "0.1.0",
        "apiVersion": 1, "kind": "python", "entry": "mikserstudio_py:Plugin"})");

    Manager m(env);
    CHECK(m.status(u"python_plugin"_s) == PluginStatus::Failed);
    CHECK(m.manager.plugin(u"python_plugin"_s)->error.contains(u"runs Lua plugins only"_s));
    REQUIRE(m.failures.size() == 1);
    CHECK(m.failures[0].first == u"python_plugin"_s);
    CHECK_FALSE(m.manager.activate(u"python_plugin"_s));
}

TEST_CASE("Discovery ignores a second plugin with the same id", "[plugins]")
{
    Environment env;
    env.installFixture(u"first"_s);
    env.installFixture(u"second"_s);
    // A plugin found in an earlier search path wins over the person's own.
    env.install(u"mine"_s, fixtureManifest(), u"user"_s);
    Manager m(env);
    CHECK(m.manager.plugins().size() == 1);
    CHECK(m.manager.plugin(kFixtureId)->manifest.directory.endsWith(u"/first"_s));
    CHECK(m.manager.plugin(kFixtureId)->source == PluginSource::BuiltIn);
    REQUIRE(m.failures.size() == 2);
    CHECK(m.failures[0].second.contains(u"ignored"_s));

    PluginManager missing(env.bus, env.settingsDir());
    missing.addSearchPath(env.temp.filePath(u"does-not-exist"_s));
    missing.discover();
    CHECK(missing.plugins().isEmpty());
}

TEST_CASE("A broken manifest cannot hide a valid plugin with the same id", "[plugins]")
{
    Environment env;
    env.installFixture(u"zz_fixture"_s);
    // Sorts before the real plugin and claims its id, but its manifest is invalid.
    env.writeManifest(u"aaa"_s, R"({"id": "mikserstudio.tests.fixture"})");

    SECTION("by claiming the id in the manifest")
    {
        Manager m(env);
        CHECK(m.status(kFixtureId) == PluginStatus::Discovered);
        CHECK(m.manager.plugin(kFixtureId)->manifest.directory.endsWith(u"/zz_fixture"_s));
        CHECK(m.status(u"aaa"_s) == PluginStatus::Failed);
        REQUIRE(m.failures.size() == 1);
        CHECK(m.failures[0].first == u"aaa"_s);
        CHECK(m.manager.activate(kFixtureId));
    }
    SECTION("by a folder named like the id")
    {
        env.writeManifest(kFixtureId, "{ not json");
        Manager m(env);
        CHECK(m.status(kFixtureId) == PluginStatus::Discovered);
        CHECK(m.manager.plugin(kFixtureId)->manifest.directory.endsWith(u"/zz_fixture"_s));
        CHECK(std::ranges::none_of(m.failures, [](const auto& failure) { return failure.first == kFixtureId; }));
        CHECK(m.manager.activate(kFixtureId));
    }
}

TEST_CASE("A plugin starts through the runtime, with its permissions and settings", "[plugins]")
{
    Environment env;
    env.installFixture();
    Manager m(env);
    CHECK_FALSE(m.manager.ready());

    m.manager.activateAll();
    CHECK(m.status(kFixtureId) == PluginStatus::Starting);
    CHECK_FALSE(m.manager.ready());
    const auto start = env.runtime.last(kFixtureId);
    REQUIRE(start);
    CHECK(start->activation.granted == QStringList{u"project.read"_s, u"plugins.manage"_s});
    CHECK(start->settings == json(R"({"mode": "quiet", "greeting": "hi"})"));

    env.runtime.startAll();
    CHECK(m.status(kFixtureId) == PluginStatus::Active);
    CHECK(m.manager.ready());

    // A reload whose new code doesn't run fails the plugin; the next one that runs starts it again.
    start->activation.failed(u"main.lua:3: oops"_s);
    CHECK(m.status(kFixtureId) == PluginStatus::Failed);
    CHECK(m.manager.plugin(kFixtureId)->error == u"main.lua:3: oops"_s);
    REQUIRE(m.failures.size() == 1);
    CHECK(start->running);
    start->activation.started();
    CHECK(m.status(kFixtureId) == PluginStatus::Active);
    CHECK(m.manager.plugin(kFixtureId)->error.isEmpty());

    REQUIRE(m.manager.setSettings(kFixtureId, json(R"({"mode": "loud"})")));
    CHECK(start->settings == json(R"({"mode": "loud", "greeting": "hi"})"));

    m.manager.deactivate(kFixtureId);
    CHECK_FALSE(start->running);
    CHECK(m.status(kFixtureId) == PluginStatus::Discovered);
    // What a stopped plugin's runtime says afterwards changes nothing.
    start->activation.started();
    CHECK(m.status(kFixtureId) == PluginStatus::Discovered);
}

TEST_CASE("Activating a failed plugin starts it anew", "[plugins]")
{
    Environment env;
    env.installFixture();
    Manager m(env);
    REQUIRE(m.manager.activate(kFixtureId));
    const auto first = env.runtime.last(kFixtureId);
    first->activation.failed(u"main.lua:1: no"_s);
    REQUIRE(m.status(kFixtureId) == PluginStatus::Failed);

    REQUIRE(m.manager.activate(kFixtureId));
    CHECK_FALSE(first->running);
    CHECK(env.runtime.starts.size() == 2);
    CHECK(m.status(kFixtureId) == PluginStatus::Starting);
    first->activation.started();  // The first start's answer is too late.
    CHECK(m.status(kFixtureId) == PluginStatus::Starting);
    env.runtime.startAll();
    CHECK(m.status(kFixtureId) == PluginStatus::Active);
}

TEST_CASE("A plugin that can't start is reported", "[plugins]")
{
    Environment env;
    env.installFixture();

    SECTION("by the runtime")
    {
        env.runtime.refusal = u"main.lua is missing."_s;
        Manager m(env);
        m.manager.activateAll();
        CHECK(m.status(kFixtureId) == PluginStatus::Failed);
        CHECK(m.manager.plugin(kFixtureId)->error == u"main.lua is missing."_s);
        REQUIRE(m.failures.size() == 1);
        CHECK(m.manager.ready());
    }
    SECTION("without a runtime")
    {
        PluginManager manager(env.bus, env.settingsDir());
        manager.addSearchPath(env.pluginsDir());
        manager.discover();
        const auto activated = manager.activate(kFixtureId);
        REQUIRE_FALSE(activated);
        CHECK(activated.error().contains(u"runs no plugins"_s));
        CHECK(manager.plugin(kFixtureId)->status == PluginStatus::Failed);
    }
}

TEST_CASE("The person's own plugins are off until enabled, and only built-in ones manage plugins", "[plugins]")
{
    Environment env;
    env.install(u"builtin"_s, fixtureManifest(u"mikserstudio.tests.builtin"_s));
    env.install(u"mine"_s, fixtureManifest(u"mikserstudio.tests.mine"_s), u"user"_s);
    env.install(u"dev"_s, fixtureManifest(u"mikserstudio.tests.dev"_s), u"development"_s);
    const QStringList readOnly{u"project.read"_s};
    {
        Manager m(env);
        CHECK(m.status(u"mikserstudio.tests.builtin"_s) == PluginStatus::Discovered);
        CHECK(m.status(u"mikserstudio.tests.mine"_s) == PluginStatus::Disabled);
        CHECK(m.status(u"mikserstudio.tests.dev"_s) == PluginStatus::Discovered);
        CHECK(m.manager.plugin(u"mikserstudio.tests.builtin"_s)->granted ==
              QStringList{u"project.read"_s, u"plugins.manage"_s});
        CHECK(m.manager.plugin(u"mikserstudio.tests.mine"_s)->granted == readOnly);
        CHECK(m.manager.plugin(u"mikserstudio.tests.mine"_s)->source == PluginSource::User);
        CHECK(m.manager.plugin(u"mikserstudio.tests.dev"_s)->granted == readOnly);

        m.manager.activateAll();
        CHECK_FALSE(env.runtime.last(u"mikserstudio.tests.mine"_s));
        REQUIRE(m.manager.setEnabled(u"mikserstudio.tests.mine"_s, true));
        REQUIRE(m.manager.activate(u"mikserstudio.tests.mine"_s));
        CHECK(env.runtime.last(u"mikserstudio.tests.mine"_s)->activation.granted == readOnly);
    }
    {
        Manager m(env);
        CHECK(m.status(u"mikserstudio.tests.mine"_s) == PluginStatus::Discovered);
        REQUIRE(m.manager.setEnabled(u"mikserstudio.tests.mine"_s, false));
    }
    Manager m(env);
    CHECK(m.status(u"mikserstudio.tests.mine"_s) == PluginStatus::Disabled);
}

TEST_CASE("Plugin settings are validated and persisted", "[plugins]")
{
    Environment env;
    env.installFixture();
    {
        Manager m(env);
        CHECK(m.manager.settings(kFixtureId) == json(R"({"mode": "quiet", "greeting": "hi"})"));
        CHECK(m.manager.settings(u"mikserstudio.tests.unknown"_s).isEmpty());

        QList<QJsonObject> changes;
        QObject::connect(&m.manager, &PluginManager::settingsChanged,
                         [&changes](const QString&, const QJsonObject& settings) { changes.append(settings); });

        const auto wrongType = m.manager.setSettings(kFixtureId, json(R"({"greeting": 5})"));
        REQUIRE_FALSE(wrongType);
        CHECK(wrongType.error().contains(u"/greeting: must be a string"_s));
        CHECK_FALSE(m.manager.setSettings(kFixtureId, json(R"({"mode": "chaos"})")));
        CHECK_FALSE(m.manager.setSettings(kFixtureId, json(R"({"unknown": 1})")));
        CHECK_FALSE(m.manager.setSettings(u"mikserstudio.tests.unknown"_s, {}));
        CHECK_FALSE(QFile::exists(env.settingsDir() + u"/mikserstudio.tests.fixture.json"_s));
        CHECK(changes.isEmpty());

        REQUIRE(m.manager.setSettings(kFixtureId, json(R"({"greeting": "yo"})")));
        CHECK(m.manager.settings(kFixtureId) == json(R"({"mode": "quiet", "greeting": "yo"})"));
        REQUIRE(changes.size() == 1);
        CHECK(changes[0] == json(R"({"mode": "quiet", "greeting": "yo"})"));
    }

    QFile file(env.settingsDir() + u"/mikserstudio.tests.fixture.json"_s);
    REQUIRE(file.open(QIODevice::ReadOnly));
    CHECK(QJsonDocument::fromJson(file.readAll()).object() == json(R"({"greeting": "yo"})"));
    file.close();
    {
        Manager m(env);
        CHECK(m.manager.settings(kFixtureId).value(u"greeting").toString() == u"yo"_s);
    }

    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(R"({"greeting": 12})");
    file.close();
    {
        Manager m(env);
        CHECK(m.manager.settings(kFixtureId) == json(R"({"mode": "quiet", "greeting": "hi"})"));
    }
}

TEST_CASE("Plugins without a settings schema take no settings", "[plugins]")
{
    Environment env;
    QJsonObject manifest = fixtureManifest();
    manifest.remove(u"settingsSchema"_s);
    env.install(u"plain"_s, manifest);
    Manager m(env);
    CHECK(m.manager.settings(kFixtureId).isEmpty());
    CHECK(m.manager.setSettings(kFixtureId, {}));
    CHECK_FALSE(m.manager.setSettings(kFixtureId, json(R"({"a": 1})")));
}

TEST_CASE("Disabled plugins stay inactive across restarts", "[plugins]")
{
    Environment env;
    env.installFixture();
    {
        Manager m(env);
        REQUIRE(m.manager.activate(kFixtureId));
        const auto start = env.runtime.last(kFixtureId);
        REQUIRE(m.manager.setEnabled(kFixtureId, false));
        CHECK(m.status(kFixtureId) == PluginStatus::Disabled);
        CHECK_FALSE(start->running);
        const auto blocked = m.manager.activate(kFixtureId);
        REQUIRE_FALSE(blocked);
        CHECK(blocked.error().contains(u"disabled"_s));
        CHECK_FALSE(m.manager.setEnabled(u"mikserstudio.tests.unknown"_s, false));
    }
    {
        Manager m(env);
        CHECK(m.status(kFixtureId) == PluginStatus::Disabled);
        m.manager.activateAll();
        CHECK(m.status(kFixtureId) == PluginStatus::Disabled);
        CHECK(m.manager.ready());
        REQUIRE(m.manager.setEnabled(kFixtureId, true));
        CHECK(m.status(kFixtureId) == PluginStatus::Discovered);
        m.manager.activateAll();
        env.runtime.startAll();
        CHECK(m.status(kFixtureId) == PluginStatus::Active);
    }
    // The destructor stopped it.
    CHECK_FALSE(env.runtime.last(kFixtureId)->running);
}

TEST_CASE("Rediscovery keeps running plugins running", "[plugins]")
{
    Environment env;
    env.installFixture();
    Manager m(env);
    int changes = 0;
    QObject::connect(&m.manager, &PluginManager::pluginsChanged, [&changes] { ++changes; });
    REQUIRE(m.manager.activate(kFixtureId));
    env.runtime.startAll();
    m.manager.discover();
    CHECK(m.status(kFixtureId) == PluginStatus::Active);
    CHECK(env.runtime.last(kFixtureId)->running);
    CHECK(changes == 3);
}
