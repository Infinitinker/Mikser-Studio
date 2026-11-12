// Where Mikser Studio keeps its files: the per-user folders, the bundled plugins and the ai/ folder.
#pragma once

#include "mikserstudio/core/UserFolders.h"

#include <QString>

namespace mikserstudio::app {

// Where Mikser Studio keeps its files. Resolved once at start-up, after core::applyInstance().
struct AppPaths {
    core::UserFolders user;  // Settings, data (the discovery file, the logs) and cache: the instance's own.
    QString pluginsDir;  // Bundled plugins, <exe>/plugins.
    QString aiDir;  // The ai/ Python package; empty when it cannot be found.
    QString ffmpegPath;  // An ffmpeg program shipped next to the executable, for the AI service; else empty.

    // MIKSERSTUDIO_AI_DIR overrides the ai/ folder; UserFolders.h names the overrides of the others.
    [[nodiscard]] static AppPaths resolve();
};

}  // namespace mikserstudio::app
