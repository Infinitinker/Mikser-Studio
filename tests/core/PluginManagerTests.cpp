// The plugin manager: discovery of valid and broken plugins, activation and deactivation, command prefixes,
// unloadable libraries, and settings that are validated, saved and kept across restarts.
#include "TestSupport.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/PluginManager.h"
#include "mikserstudio/core/UndoStack.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QTemporaryDir>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace mikserstudio::core;
using mikserstudio::tests::json;

namespace {

const QString kFixtureId = u"mikserstudio.tests.fixture"_s;

// Captures log messages for the lifetime of the object.
class LogCapture {
public:
    LogCapture()
    {
        s_messages.clear();
        m_previous = qInstallMessageHandler(&LogCapture::handle);
    }
    ~LogCapture() { qInstallMessageHandler(m_previous); }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    [[nodiscard]] bool contains(const QString& text) const
    {
        return std::ranges::any_of(s_messages, [&text](const QString& message) { return message.contains(text); });
    }

private:
    static void handle(QtMsgType, const QMessageLogContext&, const QString& message) { s_messages.append(message); }

    static inline QStringList s_messages;
    QtMessageHandler m_previous = nullptr;
};

QJsonObject fixtureManifest(const QString& entry = u"fixture"_s)
{
    QJsonObject manifest = json(R"({
        "id": "mikserstudio.tests.fixture",
        "name": "Fixture",
        "version": "1.0.0",
        "apiVersion": 1,
        "kind": "native",
        "capabilities": ["commands"],
        "settingsSchema": {
            "type": "object",
            "properties": {
                "mode": {"enum": ["ok", "fail_activate", "register_then_fail", "bad_prefix"], "default": "ok"},
                "greeting": {"type": "string", "minLength": 1, "default": "hi"}
            },
            "additionalProperties": false
        }
    })");
    manifest.insert(u"entry"_s, entry);
    return manifest;
}

struct Environment {
    QString pluginsDir() const { return temp.filePath(u"plugins"_s); }
    QString settingsDir() const { return temp.filePath(u"settings"_s); }

    // Copies a built library into <plugins>/<folder>/ and writes its manifest. Returns the copy.
    QString install(const QString& folder, const QString& library, const QJsonObject& manifest) const
    {
        const QDir dir(pluginsDir() + u'/' + folder);
        REQUIRE(QDir().mkpath(dir.path()));
        QString copy;
        if (!library.isEmpty()) {
            copy = dir.filePath(QFileInfo(library).fileName());
            REQUIRE(QFile::copy(library, copy));
        }
        writeManifest(folder, QJsonDocument(manifest).toJson());
        return copy;
    }

    QString installFixture(const QString& folder = u"fixture"_s) const
    {
        const QString library = QString::fromUtf8(MIKSERSTUDIO_TEST_FIXTURE_PLUGIN);
        return install(folder, library, fixtureManifest(QFileInfo(library).completeBaseName()));
    }

    void writeManifest(const QString& folder, const QByteArray& content) const
    {
        REQUIRE(QDir().mkpath(pluginsDir() + u'/' + folder));
        QFile file(pluginsDir() + u'/' + folder + u"/mikserstudio-plugin.json"_s);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write(content);
    }

    QTemporaryDir temp;
    UndoStack stack;
    CommandBus bus{stack};
};

struct Manager {
    explicit Manager(Environment& env)
        : manager(env.bus, env.settingsDir())
    {
        manager.addSearchPath(env.pluginsDir());
        QObject::connect(&manager, &PluginManager::pluginFailed,
                         [this](const QString& id, const QString& message) { failures.append({id, message}); });
        manager.discover();
    }

    PluginStatus status(const QString& id) const
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
    env.writeManifest(u"python_plugin"_s, R"({"id": "mikserstudio.tests.py", "name": "Py", "version": "0.1.0",
        "apiVersion": 1, "kind": "python", "entry": "mikserstudio_py:Plugin"})");
    REQUIRE(QDir().mkpath(env.pluginsDir() + u"/not_a_plugin"_s));

    Manager m(env);
    const QList<PluginState> plugins = m.manager.plugins();
    REQUIRE(plugins.size() == 4);
    CHECK(m.status(kFixtureId) == PluginStatus::Discovered);
    CHECK(m.status(u"mikserstudio.tests.py"_s) == PluginStatus::Discovered);
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

    const auto python = m.manager.activate(u"mikserstudio.tests.py"_s);
    REQUIRE_FALSE(python);
    CHECK(python.error().contains(u"Python"_s));
    CHECK_FALSE(m.manager.activate(u"bad_manifest"_s));
    CHECK_FALSE(m.manager.activate(u"mikserstudio.tests.unknown"_s));
}

TEST_CASE("Discovery ignores a second plugin with the same id", "[plugins]")
{
    Environment env;
    env.installFixture(u"first"_s);
    env.installFixture(u"second"_s);
    Manager m(env);
    CHECK(m.manager.plugins().size() == 1);
    CHECK(m.manager.plugin(kFixtureId)->manifest.directory.endsWith(u"/first"_s));
    REQUIRE(m.failures.size() == 1);
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

TEST_CASE("A native plugin activates, runs commands and deactivates cleanly", "[plugins]")
{
    Environment env;
    const QString library = env.installFixture();
    LogCapture logs;
    Manager m(env);

    REQUIRE(m.manager.activate(kFixtureId));
    CHECK(m.status(kFixtureId) == PluginStatus::Active);
    CHECK(logs.contains(u"[mikserstudio.tests.fixture] fixture: activate ok"_s));
    REQUIRE(env.bus.contains(kFixtureId + u".echo"_s));
    CHECK(env.bus.spec(kFixtureId + u".echo"_s)->category == u"Fixture"_s);
    CHECK_FALSE(env.bus.spec(kFixtureId + u".echo"_s)->undoable);
    CHECK(env.bus.spec(kFixtureId + u".echo"_s)->readOnly);
    CHECK(env.bus.spec(kFixtureId + u".echo"_s)->confirmation.isEmpty());
    CHECK_FALSE(env.bus.spec(kFixtureId + u".run"_s)->readOnly);
    CHECK(env.bus.spec(kFixtureId + u".run"_s)->confirmation == u"Run another command?"_s);
    CHECK(m.manager.activate(kFixtureId));  // Already active is fine.

    SECTION("commands are validated by the host and answered by the plugin")
    {
        const auto echoed = env.bus.execute(kFixtureId + u".echo"_s, json(R"({"text": "ping"})"));
        REQUIRE(echoed);
        CHECK(*echoed == json(R"({"echo": "ping", "greeting": "hi"})"));
        const auto invalid = env.bus.execute(kFixtureId + u".echo"_s);
        REQUIRE_FALSE(invalid);
        CHECK(invalid.error().code == CommandErrorCode::InvalidParams);
    }
    SECTION("plugins can execute other commands through the host")
    {
        const auto nested = env.bus.execute(
            kFixtureId + u".run"_s, json(R"({"command": "mikserstudio.tests.fixture.echo", "params": {"text": "x"}})"));
        REQUIRE(nested);
        CHECK(nested->value(u"echo").toString() == u"x"_s);
        const auto unknown = env.bus.execute(kFixtureId + u".run"_s, json(R"({"command": "no.such_command"})"));
        REQUIRE_FALSE(unknown);
        CHECK(unknown.error().message.contains(u"Unknown command \"no.such_command\""_s));
    }
    SECTION("get_settings returns the current settings")
    {
        REQUIRE(m.manager.setSettings(kFixtureId, json(R"({"greeting": "hey"})")));
        const auto echoed = env.bus.execute(kFixtureId + u".echo"_s, json(R"({"text": "a"})"));
        REQUIRE(echoed);
        CHECK(echoed->value(u"greeting").toString() == u"hey"_s);
    }

    m.manager.deactivate(kFixtureId);
    CHECK(m.status(kFixtureId) == PluginStatus::Discovered);
    CHECK(logs.contains(u"fixture: deactivate"_s));
    CHECK_FALSE(env.bus.contains(kFixtureId + u".echo"_s));
    CHECK_FALSE(env.bus.contains(kFixtureId + u".run"_s));
    CHECK(QFile::remove(library));  // The library is unloaded (Windows keeps loaded DLLs locked).
}

TEST_CASE("A plugin that fails to activate is reported and unloaded", "[plugins]")
{
    Environment env;
    const QString library = env.installFixture();
    Manager m(env);

    SECTION("failure before registering")
    {
        REQUIRE(m.manager.setSettings(kFixtureId, json(R"({"mode": "fail_activate"})")));
        const auto result = m.manager.activate(kFixtureId);
        REQUIRE_FALSE(result);
        CHECK(result.error().contains(u"fixture refused to start"_s));
    }
    SECTION("failure after registering a command")
    {
        REQUIRE(m.manager.setSettings(kFixtureId, json(R"({"mode": "register_then_fail"})")));
        const auto result = m.manager.activate(kFixtureId);
        REQUIRE_FALSE(result);
        CHECK(result.error().contains(u"fixture failed after registering"_s));
        CHECK_FALSE(env.bus.contains(kFixtureId + u".echo"_s));
    }
    CHECK(m.status(kFixtureId) == PluginStatus::Failed);
    CHECK_FALSE(m.manager.plugin(kFixtureId)->error.isEmpty());
    REQUIRE(m.failures.size() == 1);
    CHECK(m.failures[0].first == kFixtureId);
    CHECK(QFile::remove(library));

    m.manager.activateAll();  // Failed plugins are not retried until the next discovery.
    CHECK(m.failures.size() == 1);
    m.manager.discover();
    CHECK(m.status(kFixtureId) == PluginStatus::Discovered);
}

TEST_CASE("Plugin commands must start with the plugin id", "[plugins]")
{
    Environment env;
    env.installFixture();
    Manager m(env);
    REQUIRE(m.manager.setSettings(kFixtureId, json(R"({"mode": "bad_prefix"})")));
    REQUIRE(m.manager.activate(kFixtureId));
    CHECK_FALSE(env.bus.contains(u"other.plugin.command"_s));
    const auto lastError = env.bus.execute(kFixtureId + u".last_error"_s);
    REQUIRE(lastError);
    CHECK(lastError->value(u"error").toString().contains(u"must start with the plugin id"_s));
    CHECK(lastError->value(u"error").toString().contains(u"mikserstudio.tests.fixture."_s));
}

TEST_CASE("Unloadable libraries are reported without crashing", "[plugins]")
{
    Environment env;
    QJsonObject manifest = fixtureManifest(u"missing"_s);
    env.install(u"missing"_s, QString(), manifest);

    const QString noEntry = QString::fromUtf8(MIKSERSTUDIO_TEST_NOENTRY_PLUGIN);
    manifest = fixtureManifest(QFileInfo(noEntry).completeBaseName());
    manifest.insert(u"id"_s, u"mikserstudio.tests.noentry"_s);
    const QString noEntryCopy = env.install(u"noentry"_s, noEntry, manifest);

    const QString wrongApi = QString::fromUtf8(MIKSERSTUDIO_TEST_WRONGAPI_PLUGIN);
    manifest = fixtureManifest(QFileInfo(wrongApi).completeBaseName());
    manifest.insert(u"id"_s, u"mikserstudio.tests.wrongapi"_s);
    const QString wrongApiCopy = env.install(u"wrongapi"_s, wrongApi, manifest);

    QFile garbage(env.pluginsDir() + u"/garbage/garbage.dll"_s);
    manifest = fixtureManifest(u"garbage"_s);
    manifest.insert(u"id"_s, u"mikserstudio.tests.garbage"_s);
    env.install(u"garbage"_s, QString(), manifest);
    REQUIRE(garbage.open(QIODevice::WriteOnly));
    garbage.write("this is not a library");
    garbage.close();

    Manager m(env);
    m.manager.activateAll();
    CHECK(m.status(kFixtureId) == PluginStatus::Failed);
    CHECK(m.manager.plugin(kFixtureId)->error.contains(u"Could not load the plugin library"_s));
    CHECK(m.status(u"mikserstudio.tests.noentry"_s) == PluginStatus::Failed);
    CHECK(m.manager.plugin(u"mikserstudio.tests.noentry"_s)->error.contains(u"mikserstudio_plugin_v1"_s));
    CHECK(m.status(u"mikserstudio.tests.wrongapi"_s) == PluginStatus::Failed);
    CHECK(m.manager.plugin(u"mikserstudio.tests.wrongapi"_s)->error.contains(u"API version 99"_s));
    CHECK(m.status(u"mikserstudio.tests.garbage"_s) == PluginStatus::Failed);
    CHECK(m.failures.size() == 4);
    CHECK(QFile::remove(noEntryCopy));
    CHECK(QFile::remove(wrongApiCopy));
}

TEST_CASE("Plugin settings are validated and persisted", "[plugins]")
{
    Environment env;
    env.installFixture();
    {
        Manager m(env);
        CHECK(m.manager.settings(kFixtureId) == json(R"({"mode": "ok", "greeting": "hi"})"));
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
        CHECK(m.manager.settings(kFixtureId) == json(R"({"mode": "ok", "greeting": "yo"})"));
        REQUIRE(changes.size() == 1);
        CHECK(changes[0] == json(R"({"mode": "ok", "greeting": "yo"})"));
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
        CHECK(m.manager.settings(kFixtureId) == json(R"({"mode": "ok", "greeting": "hi"})"));
    }
}

TEST_CASE("Plugins without a settings schema take no settings", "[plugins]")
{
    Environment env;
    QJsonObject manifest = fixtureManifest();
    manifest.remove(u"settingsSchema"_s);
    env.install(u"plain"_s, QString(), manifest);
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
        REQUIRE(m.manager.setEnabled(kFixtureId, false));
        CHECK(m.status(kFixtureId) == PluginStatus::Disabled);
        CHECK_FALSE(env.bus.contains(kFixtureId + u".echo"_s));
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
        REQUIRE(m.manager.setEnabled(kFixtureId, true));
        CHECK(m.status(kFixtureId) == PluginStatus::Discovered);
        m.manager.activateAll();
        CHECK(m.status(kFixtureId) == PluginStatus::Active);
    }
    CHECK_FALSE(env.bus.contains(kFixtureId + u".echo"_s));  // The destructor deactivated it.
}

TEST_CASE("Rediscovery keeps active plugins active", "[plugins]")
{
    Environment env;
    env.installFixture();
    Manager m(env);
    int changes = 0;
    QObject::connect(&m.manager, &PluginManager::pluginsChanged, [&changes] { ++changes; });
    REQUIRE(m.manager.activate(kFixtureId));
    m.manager.discover();
    CHECK(m.status(kFixtureId) == PluginStatus::Active);
    CHECK(env.bus.contains(kFixtureId + u".echo"_s));
    CHECK(changes == 2);
}
