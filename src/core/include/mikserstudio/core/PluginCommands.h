// The plugins.* commands and the plugins.changed event: listing the plugins, turning them on and
// off, changing their settings and finding new ones, for the Plugins panel, built-in plugins and
// agents.
#pragma once

#include <QJsonObject>

namespace mikserstudio::core {

class CommandBus;
class PluginManager;
struct PluginState;

// Registers plugins.list, plugins.set_enabled, plugins.set_settings and plugins.rescan, which need
// the plugins.manage permission, and publishes plugins.changed, with whether the plugins are ready,
// each time a plugin is found, starts, stops, fails or is turned on or off.
void registerPluginCommands(CommandBus& bus, PluginManager& plugins);

// A plugin as plugins.list lists it: its id, name, version, description, status ("discovered",
// "starting", "active", "failed" or "disabled"), error, source ("builtin", "user" or
// "development"), the permissions it runs with and its settings schema.
[[nodiscard]] QJsonObject toJson(const PluginState& state);

}  // namespace mikserstudio::core
