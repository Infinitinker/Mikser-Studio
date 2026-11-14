// The per-user folders from QStandardPaths and the environment overrides.
#include "mikserstudio/core/UserFolders.h"

#include <QDir>
#include <QStandardPaths>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

namespace {

QString fromEnvOr(const char* name, QStandardPaths::StandardLocation location)
{
    const QString value = qEnvironmentVariable(name);
    return value.isEmpty() ? QStandardPaths::writableLocation(location) : QDir(value).absolutePath();
}

}  // namespace

UserFolders UserFolders::resolve()
{
    UserFolders folders;
    folders.config = fromEnvOr("MIKSERSTUDIO_CONFIG_DIR", QStandardPaths::AppConfigLocation);
    folders.data = fromEnvOr("MIKSERSTUDIO_DATA_DIR", QStandardPaths::AppLocalDataLocation);
    folders.cache = fromEnvOr("MIKSERSTUDIO_CACHE_DIR", QStandardPaths::CacheLocation);
    if (!folders.data.isEmpty()) {
        folders.logs = QDir(folders.data).filePath(u"logs"_s);
        folders.discoveryFile = QDir(folders.data).filePath(u"api.json"_s);
    }
    return folders;
}

}  // namespace mikserstudio::core
