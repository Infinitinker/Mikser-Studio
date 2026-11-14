// The plugins' specs: each Lua file in the spec/ folder of a built-in plugin or of the example runs,
// with every permission, once the Lua host has started its plugin. A spec passes when it runs to its
// end, and fails with the error it raises (docs/plugins.md, "Tests").
#include "LuaTestSupport.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/JobManager.h"
#include "mikserstudio/core/PluginCommands.h"
#include "mikserstudio/core/PluginManager.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/core/ProjectSnapshot.h"
#include "mikserstudio/core/UndoStack.h"
#include "mikserstudio/lua/LuaRuntime.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QTemporaryDir>

#include <chrono>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;
using namespace mikserstudio::core;
using mikserstudio::tests::waitFor;

namespace {

struct Spec {
    QString pluginFolder;
    QString entry;  // spec/<name>.lua
    PluginSource source;
};

// Every spec, plugin by plugin and in name order.
QList<Spec> findSpecs()
{
    const QDir plugins(QString::fromUtf8(MIKSERSTUDIO_PLUGINS_SOURCE));
    QList<Spec> found;
    for (const auto& [folder, source] :
         {std::pair{u"builtin"_s, PluginSource::BuiltIn}, std::pair{u"examples"_s, PluginSource::Development}}) {
        const QDir parent(plugins.filePath(folder));
        for (const QString& plugin : parent.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QDir specs(parent.filePath(plugin + u"/spec"_s));
            for (const QString& file : specs.entryList({u"*.lua"_s}, QDir::Files, QDir::Name))
                found.append(
                    Spec{.pluginFolder = parent.filePath(plugin), .entry = u"spec/"_s + file, .source = source});
        }
    }
    return found;
}

struct Environment {
    Environment()
    {
        manager.setRuntime(&runtime);
        registerPluginCommands(bus, manager);
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

// Starts the spec's plugin, then runs the spec; nothing when it passes, otherwise why it failed.
std::optional<QString> run(Environment& env, const Spec& spec)
{
    env.manager.addSearchPath(QFileInfo(spec.pluginFolder).path(), spec.source);
    env.manager.discover();
    QString id;
    for (const PluginState& state : env.manager.plugins()) {
        if (QDir(state.manifest.directory) == QDir(spec.pluginFolder))
            id = state.manifest.id;
    }
    if (id.isEmpty())
        return u"The plugin in %1 wasn't found."_s.arg(spec.pluginFolder);
    if (const auto activated = env.manager.activate(id); !activated)
        return activated.error();
    if (!waitFor([&env, &id] { return env.manager.plugin(id)->status != PluginStatus::Starting; }))
        return u"The plugin didn't start in time."_s;
    if (const PluginState* state = env.manager.plugin(id); state->status != PluginStatus::Active)
        return u"The plugin didn't start: %1"_s.arg(state->error);

    PluginManifest manifest;
    manifest.id = id + u".spec"_s;
    manifest.name = spec.entry;
    manifest.version = u"1.0.0"_s;
    manifest.apiVersion = kPluginApiVersion;
    manifest.entry = spec.entry;
    manifest.permissions = allPermissions();
    manifest.directory = spec.pluginFolder;
    bool done = false;
    std::optional<QString> failure;
    const auto instance = env.runtime.activate(manifest,
                                               PluginActivation{.granted = allPermissions(),
                                                                .settings = {},
                                                                .started = [&done] { done = true; },
                                                                .failed =
                                                                    [&done, &failure](const QString& message) {
                                                                        failure = message;
                                                                        done = true;
                                                                    }});
    if (!instance)
        return instance.error();
    if (!waitFor([&done] { return done; }, std::chrono::seconds(60)))
        return u"The spec didn't end within a minute."_s;
    return failure;
}

}  // namespace

TEST_CASE("Every plugin passes its specs", "[plugins]")
{
    const QList<Spec> specs = findSpecs();
    REQUIRE_FALSE(specs.isEmpty());  // The example has one.
    for (const Spec& spec : specs) {
        DYNAMIC_SECTION(QFileInfo(spec.pluginFolder).fileName().toStdString() << '/' << spec.entry.toStdString())
        {
            Environment env;
            const std::optional<QString> failure = run(env, spec);
            INFO(failure.value_or(QString()).toStdString());
            CHECK_FALSE(failure.has_value());
        }
    }
}
