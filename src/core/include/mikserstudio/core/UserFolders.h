// The per-user folders of the running instance (Instance.h): its settings, its data with the API's
// discovery file and the logs, and its cache. Everything Mikser Studio writes for the user goes into
// one of them.
#pragma once

#include <QString>

namespace mikserstudio::core {

struct UserFolders {
    QString config;  // Workspaces, the window's state, plugin settings. MIKSERSTUDIO_CONFIG_DIR.
    QString data;  // The discovery file and the logs. MIKSERSTUDIO_DATA_DIR.
    QString cache;  // AI results. MIKSERSTUDIO_CACHE_DIR.
    QString logs;  // <data>/logs.
    QString discoveryFile;  // <data>/api.json, which tells other programs how to reach the local API.

    // The folders of the application name applyInstance() set, as QStandardPaths gives them, or the
    // folders the environment variables above name. On Windows all three are
    // %LOCALAPPDATA%\<instance name>, the cache in its "cache" folder.
    [[nodiscard]] static UserFolders resolve();
};

}  // namespace mikserstudio::core
