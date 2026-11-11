#pragma once

#include "PluginHost.h"
#include "mikserstudio/core/PluginManager.h"

#include <mikserstudio/plugin_api.h>

#include <QLibrary>

#include <expected>
#include <memory>

namespace mikserstudio::core {

class CommandBus;

// A loaded and activated native plugin library. Destroying it unregisters the plugin's commands,
// calls its deactivate() and unloads the library, in that order.
class NativePlugin {
public:
    // Loads <manifest.directory>/<manifest.entry>, checks the exported table and calls activate().
    // On any failure nothing stays registered or loaded and the reason is returned.
    [[nodiscard]] static std::expected<std::unique_ptr<NativePlugin>, QString> activate(
        const PluginManifest& manifest, CommandBus& bus, PluginHost::SettingsProvider settings);

    ~NativePlugin();
    NativePlugin(const NativePlugin&) = delete;
    NativePlugin& operator=(const NativePlugin&) = delete;

    [[nodiscard]] QStringList commands() const { return m_host->commands(); }

private:
    NativePlugin() = default;

    std::unique_ptr<QLibrary> m_library;
    const MikserStudioPluginV1* m_table = nullptr;
    void* m_state = nullptr;
    std::unique_ptr<PluginHost> m_host;
};

}  // namespace mikserstudio::core
