// Where Mikser Studio keeps its files: the per-user folders and the bundled plugins.
#pragma once

#include "mikserstudio/core/UserFolders.h"

#include <QString>

namespace mikserstudio::app {

// Where Mikser Studio keeps its files. Resolved once at start-up, after core::applyInstance().
struct AppPaths {
    core::UserFolders user;  // Settings, data (the discovery file, the logs) and cache: the instance's own.
    QString pluginsDir;  // Bundled plugins, <exe>/plugins.

    // UserFolders.h names the environment variables that override the per-user folders.
    [[nodiscard]] static AppPaths resolve();
};

}  // namespace mikserstudio::app
