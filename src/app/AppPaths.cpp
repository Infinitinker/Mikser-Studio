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
    return paths;
}

}  // namespace mikserstudio::app
