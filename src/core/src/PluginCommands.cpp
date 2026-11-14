// The plugins.* commands and the plugins.changed event.
#include "mikserstudio/core/PluginCommands.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/PluginManager.h"

#include <QJsonArray>
#include <QSet>

#include <tuple>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

namespace {

const QString kManage = u"plugins.manage"_s;

QString toString(PluginStatus status)
{
    switch (status) {
    case PluginStatus::Starting:
        return u"starting"_s;
    case PluginStatus::Active:
        return u"active"_s;
    case PluginStatus::Failed:
        return u"failed"_s;
    case PluginStatus::Disabled:
        return u"disabled"_s;
    case PluginStatus::Discovered:
        break;
    }
    return u"discovered"_s;
}

QString toString(PluginSource source)
{
    switch (source) {
    case PluginSource::User:
        return u"user"_s;
    case PluginSource::Development:
        return u"development"_s;
    case PluginSource::BuiltIn:
        break;
    }
    return u"builtin"_s;
}

QJsonObject idParam()
{
    return QJsonObject{{u"type"_s, u"string"_s}, {u"description"_s, u"The plugin's id."_s}};
}

QJsonObject objectSchema(const QJsonObject& properties, const QStringList& required)
{
    return QJsonObject{{u"type"_s, u"object"_s},
                       {u"properties"_s, properties},
                       {u"required"_s, QJsonArray::fromStringList(required)},
                       {u"additionalProperties"_s, false}};
}

CommandSpec spec(const QString& id, const QString& title, const QString& description)
{
    CommandSpec spec;
    spec.id = id;
    spec.title = title;
    spec.description = description;
    spec.category = u"Plugins"_s;
    spec.permission = kManage;
    return spec;
}

QJsonObject listJson(const PluginManager& plugins)
{
    QJsonArray list;
    for (const PluginState& state : plugins.plugins()) {
        QJsonObject plugin = toJson(state);
        plugin.insert(u"settings"_s, plugins.settings(state.manifest.id));
        list.append(plugin);
    }
    return QJsonObject{{u"plugins"_s, list}, {u"ready"_s, plugins.ready()}};
}

// A plugin that is in the chain of who runs this command, so stopping it now would pull its code
// from under the call.
bool runsThrough(const CommandBus& bus, const QString& pluginId)
{
    const CallContext* running = bus.runningContext();
    return running && running->origins.contains(u"plugin:"_s + pluginId);
}

}  // namespace

QJsonObject toJson(const PluginState& state)
{
    return QJsonObject{
        {u"id"_s, state.manifest.id},
        {u"name"_s, state.manifest.name},
        {u"version"_s, state.manifest.version},
        {u"description"_s, state.manifest.description},
        {u"status"_s, toString(state.status)},
        {u"error"_s, state.error},
        {u"source"_s, toString(state.source)},
        {u"permissions"_s, QJsonArray::fromStringList(state.granted)},
        {u"settingsSchema"_s, state.manifest.settingsSchema},
    };
}

void registerPluginCommands(CommandBus& bus, PluginManager& plugins)
{
    CommandSpec list = spec(u"plugins.list"_s, u"List plugins"_s,
                            u"Lists the plugins: each one's status, where it comes from, the permissions it runs "
                            "with and its settings, and whether the plugins have started (ready)."_s);
    list.readOnly = true;
    list.resultSchema = objectResultSchema({{u"plugins"_s, u"array"_s}, {u"ready"_s, u"boolean"_s}});
    bus.registerCommand(
        list, [&plugins](const QJsonObject&) -> CommandResult { return CommandOutcome{listJson(plugins), nullptr}; });

    CommandSpec setEnabled = spec(u"plugins.set_enabled"_s, u"Turn plugin on or off"_s,
                                  u"Turns a plugin on, which starts it, or off, which stops it, and keeps the choice. "
                                  "Turning on one of the person's own plugins grants it the permissions it asks for, "
                                  "so it needs the person's agreement (grant)."_s);
    setEnabled.paramsSchema = objectSchema(
        {{u"id"_s, idParam()},
         {u"enabled"_s, QJsonObject{{u"type"_s, u"boolean"_s}, {u"description"_s, u"On or off."_s}}},
         {u"grant"_s, QJsonObject{{u"type"_s, u"boolean"_s},
                                  {u"default"_s, false},
                                  {u"description"_s, u"The person agreed to grant the plugin its permissions."_s}}}},
        {u"id"_s, u"enabled"_s});
    setEnabled.confirmationParam = u"grant"_s;
    setEnabled.resultSchema = objectResultSchema({{u"status"_s, u"string"_s}, {u"error"_s, u"string"_s}});
    bus.registerCommand(setEnabled, [&bus, &plugins](const QJsonObject& params) -> CommandResult {
        const QString id = params.value(u"id").toString();
        const bool enabled = params.value(u"enabled").toBool();
        if (!plugins.plugin(id))
            return commandFailure(CommandErrorCode::InvalidParams,
                                  u"There is no plugin “%1”. Run plugins.list to see the plugins."_s.arg(id));
        const PluginState* current = plugins.plugin(id);
        if (enabled && current->source == PluginSource::User && current->status == PluginStatus::Disabled &&
            !params.value(u"grant").toBool()) {
            const QString permissions = current->granted.isEmpty() ? u"none"_s : current->granted.join(u", "_s);
            return commandFailure(
                CommandErrorCode::NeedsConfirmation,
                u"Turn on “%1”? It gets these permissions: %2."_s.arg(current->manifest.name, permissions));
        }
        if (!enabled && runsThrough(bus, id))
            return commandFailure(CommandErrorCode::InvalidParams,
                                  u"A plugin can't turn itself off; the person can, in the Plugins panel."_s);
        if (const auto set = plugins.setEnabled(id, enabled); !set)
            return commandFailure(CommandErrorCode::Failed, set.error());
        if (enabled && plugins.plugin(id)->status == PluginStatus::Discovered)
            std::ignore = plugins.activate(id);  // A failure shows in the status and error.
        const PluginState* state = plugins.plugin(id);
        return CommandOutcome{QJsonObject{{u"status"_s, toString(state->status)}, {u"error"_s, state->error}}, nullptr};
    });

    CommandSpec setSettings = spec(u"plugins.set_settings"_s, u"Change plugin settings"_s,
                                   u"Replaces a plugin's settings, which must match its settings schema; what they "
                                   "leave out takes the schema's defaults. A running plugin gets them at once."_s);
    setSettings.paramsSchema = objectSchema(
        {{u"id"_s, idParam()},
         {u"settings"_s, QJsonObject{{u"type"_s, u"object"_s}, {u"description"_s, u"The new settings."_s}}}},
        {u"id"_s, u"settings"_s});
    setSettings.resultSchema = objectResultSchema({{u"settings"_s, u"object"_s}});
    bus.registerCommand(setSettings, [&plugins](const QJsonObject& params) -> CommandResult {
        const QString id = params.value(u"id").toString();
        if (!plugins.plugin(id))
            return commandFailure(CommandErrorCode::InvalidParams,
                                  u"There is no plugin “%1”. Run plugins.list to see the plugins."_s.arg(id));
        if (const auto set = plugins.setSettings(id, params.value(u"settings").toObject()); !set)
            return commandFailure(CommandErrorCode::InvalidParams, set.error());
        return CommandOutcome{QJsonObject{{u"settings"_s, plugins.settings(id)}}, nullptr};
    });

    CommandSpec rescan = spec(u"plugins.rescan"_s, u"Rescan plugins"_s,
                              u"Reads the plugin folders again and starts the plugins found that are on. Running "
                              "plugins keep running."_s);
    rescan.resultSchema = objectResultSchema({{u"plugins"_s, u"array"_s}, {u"ready"_s, u"boolean"_s}});
    bus.registerCommand(rescan, [&plugins](const QJsonObject&) -> CommandResult {
        QSet<QString> known;
        for (const PluginState& state : plugins.plugins())
            known.insert(state.manifest.id);
        plugins.discover();
        for (const PluginState& state : plugins.plugins()) {
            if (!known.contains(state.manifest.id) && state.status == PluginStatus::Discovered)
                std::ignore = plugins.activate(state.manifest.id);  // A failure shows in the list.
        }
        return CommandOutcome{listJson(plugins), nullptr};
    });

    QObject::connect(&plugins, &PluginManager::pluginsChanged, &bus,
                     [&bus, &plugins] { bus.publish(u"plugins.changed"_s, {{u"ready"_s, plugins.ready()}}); });
}

}  // namespace mikserstudio::core
