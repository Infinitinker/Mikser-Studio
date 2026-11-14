// Starting Lua plugins: checking the entry file, then handing the plugin to a LuaPlugin.
#include "mikserstudio/lua/LuaRuntime.h"

#include "LuaPlugin.h"

#include <QDir>
#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace mikserstudio::lua {

LuaRuntime::LuaRuntime(LuaRuntimeOptions options)
    : m_options(std::move(options))
{
}

std::expected<std::unique_ptr<core::PluginInstance>, QString> LuaRuntime::activate(const core::PluginManifest& manifest,
                                                                                   core::PluginActivation activation)
{
    const QFileInfo entry(QDir(manifest.directory).filePath(manifest.entry));
    if (!entry.isFile()) {
        return std::unexpected(u"The plugin “%1” has no entry file %2. Reinstall the plugin."_s.arg(
            manifest.name, QDir::toNativeSeparators(entry.filePath())));
    }
    auto plugin = std::make_unique<LuaPlugin>(m_options, manifest, std::move(activation));
    plugin->start();
    return plugin;
}

}  // namespace mikserstudio::lua
