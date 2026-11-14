// Where Mikser Studio keeps its files: the per-user folders and the plugins' folders.
#pragma once

#include "mikserstudio/core/UserFolders.h"

#include <QString>
#include <QStringList>

namespace mikserstudio::app {

// Where Mikser Studio keeps its files. Resolved once at start-up, after core::applyInstance().
struct AppPaths {
    core::UserFolders user;  // Settings, data (the discovery file, the logs) and cache: the instance's own.
    QString pluginsDir;  // The built-in plugins, <exe>/plugins.
    QString userPluginsDir;  // The person's own plugins, <data>/plugins.
    // Folders of plugins in development, from MIKSERSTUDIO_PLUGIN_PATH (separated like PATH).
    QStringList developmentPluginsDirs;

    // UserFolders.h names the environment variables that override the per-user folders.
    [[nodiscard]] static AppPaths resolve();
};

}  // namespace mikserstudio::app
