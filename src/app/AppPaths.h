#pragma once

#include <QString>

namespace mikserstudio::app {

// Where Mikser Studio keeps its files. Resolved once at start-up, after the application object exists.
struct AppPaths {
    QString configDir;    // Layouts, workspaces, plugin settings.
    QString cacheDir;     // Generated media (AI results), proxies.
    QString pluginsDir;   // Bundled plugins, <exe>/plugins.
    QString aiDir;        // The ai/ Python package; empty when it cannot be found.

    // MIKSERSTUDIO_CONFIG_DIR / MIKSERSTUDIO_CACHE_DIR / MIKSERSTUDIO_AI_DIR override the defaults.
    [[nodiscard]] static AppPaths resolve();
};

}  // namespace mikserstudio::app
