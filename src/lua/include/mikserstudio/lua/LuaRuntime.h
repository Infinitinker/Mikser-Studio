// The Lua host: runs each Lua plugin in a sandboxed Lua 5.4 state of its own, on its own thread,
// with the ms API (docs/plugins.md).
#pragma once

#include "mikserstudio/core/PluginRuntime.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>

namespace mikserstudio::core {
class CommandBus;
class JobManager;
struct ProjectSnapshot;
}  // namespace mikserstudio::core

namespace mikserstudio::lua {

struct LuaRuntimeOptions {
    core::CommandBus* bus = nullptr;
    core::JobManager* jobs = nullptr;
    // The project as it is now; called from the plugins' threads.
    std::function<std::shared_ptr<const core::ProjectSnapshot>()> snapshot;
    // Reload a plugin when one of its files changes.
    bool hotReload = false;
    // A state that needs more memory than this gets "not enough memory" errors.
    std::size_t memoryLimit = std::size_t{256} * 1024 * 1024;
    // Plugin code that runs this long without waiting (for ms.run, a job or an event) is stopped
    // with an error. It can't stop a single long C call, such as a pathological string.find.
    std::chrono::milliseconds runLimit{5000};
    // How long stopping a plugin waits for its thread; a thread that doesn't stop by then is left
    // running, since it can't be killed safely.
    std::chrono::milliseconds stopTimeout{2000};
};

class LuaRuntime final : public core::PluginRuntime {
public:
    explicit LuaRuntime(LuaRuntimeOptions options);

    [[nodiscard]] std::expected<std::unique_ptr<core::PluginInstance>, QString> activate(
        const core::PluginManifest& manifest, core::PluginActivation activation) override;

private:
    LuaRuntimeOptions m_options;
};

}  // namespace mikserstudio::lua
