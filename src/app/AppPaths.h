#pragma once

#include <QString>

namespace mikserstudio::app {

// Where Mikser Studio keeps its files. Resolved once at start-up, after the application object exists.
struct AppPaths {
    QString configDir;    // Layouts, workspaces, plugin settings.
    QString cacheDir;     // Generated media (AI results), proxies.
    QString dataDir;      // Per-user data, e.g. the AI service's Python environment.
    QString pluginsDir;   // Bundled plugins, <exe>/plugins.
    QString aiDir;        // The ai/ uv project; empty when it cannot be found.
    bool devBuild = false;  // Running from a build tree, with the sources next to it.

    // MIKSERSTUDIO_CONFIG_DIR / MIKSERSTUDIO_CACHE_DIR / MIKSERSTUDIO_AI_DIR override the defaults.
    [[nodiscard]] static AppPaths resolve();
};

}  // namespace mikserstudio::app
