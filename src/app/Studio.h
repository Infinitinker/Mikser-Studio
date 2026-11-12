// Everything except the UI, wired together: the core, the playback engine, the AI service, plugins and the
// local API.
#pragma once

#include "mikserstudio/api/ApiServer.h"
#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/PluginManager.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/core/UndoStack.h"
#include "mikserstudio/engine/ClipPlayer.h"
#include "mikserstudio/engine/MltMediaProbe.h"
#include "mikserstudio/engine/TimelinePlayer.h"
#include "mikserstudio/services/AiClient.h"
#include "mikserstudio/services/SidecarManager.h"

#include <QString>

#include <memory>

namespace mikserstudio::app {

struct AppPaths;

struct StudioOptions {
    bool audio = true;  // false: players run silent (headless, tests).
    bool api = true;
    quint16 apiPort = 0;  // 0: a free port, published in the discovery file.
};

// Everything except the UI: the core, the playback engine, the AI service, plugins and the API.
//
// Members are declared in construction order so they are destroyed in reverse: the API and the
// plugins go before the bus, the players before the project they play. Playback is paused before
// any of that, and a new or opened project closes the source monitor.
class Studio final {
public:
    Studio(const AppPaths& paths, StudioOptions options);
    ~Studio();
    Studio(const Studio&) = delete;
    Studio& operator=(const Studio&) = delete;

    // Starts the API and activates plugins (after the API, so its clients see their commands).
    void start();

    [[nodiscard]] core::CommandBus& bus() { return m_bus; }
    [[nodiscard]] core::ProjectModel& project() { return m_project; }
    [[nodiscard]] core::PluginManager& plugins() { return m_plugins; }
    [[nodiscard]] services::SidecarManager& sidecar() { return m_sidecar; }

    // Null when MLT could not be initialized; engineError() then says why.
    [[nodiscard]] engine::ClipPlayer* sourcePlayer() const { return m_source.get(); }
    [[nodiscard]] engine::TimelinePlayer* programPlayer() const { return m_program.get(); }
    [[nodiscard]] QString engineError() const { return m_engineError; }
    [[nodiscard]] const api::ApiServer* apiServer() const { return m_api.get(); }

private:
    StudioOptions m_options;
    core::UndoStack m_undo;
    core::CommandBus m_bus{m_undo};
    core::ProjectModel m_project;

    QString m_engineError;
    std::unique_ptr<core::MediaProbe> m_probe;
    std::unique_ptr<engine::ClipPlayer> m_source;
    std::unique_ptr<engine::TimelinePlayer> m_program;

    services::SidecarManager m_sidecar;
    services::AiClient m_aiClient;

    core::PluginManager m_plugins;
    std::unique_ptr<api::ApiServer> m_api;
};

}  // namespace mikserstudio::app
