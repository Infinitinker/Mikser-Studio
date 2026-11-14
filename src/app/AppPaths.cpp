// Finding the per-user and plugins folders from the environment overrides, the standard locations and the
// install layout.
#include "AppPaths.h"

#include <QCoreApplication>
#include <QDir>

using namespace Qt::StringLiterals;

namespace mikserstudio::app {

AppPaths AppPaths::resolve()
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    AppPaths paths;
    paths.user = core::UserFolders::resolve();
    paths.pluginsDir = appDir.filePath(u"plugins"_s);
    paths.userPluginsDir = QDir(paths.user.data).filePath(u"plugins"_s);
    paths.developmentPluginsDirs =
        qEnvironmentVariable("MIKSERSTUDIO_PLUGIN_PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts);
    return paths;
}

}  // namespace mikserstudio::app
