// Finding the per-user, plugins and ai/ folders from the environment overrides, the standard locations
// and the install layout.
#include "AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace mikserstudio::app {

namespace {

QString exeSuffix()
{
#ifdef Q_OS_WIN
    return u".exe"_s;
#else
    return {};
#endif
}

bool isAiProject(const QString& dir)
{
    return QFileInfo::exists(QDir(dir).filePath(u"pyproject.toml"_s));
}

}  // namespace

AppPaths AppPaths::resolve()
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    AppPaths paths;
    paths.user = core::UserFolders::resolve();
    paths.pluginsDir = appDir.filePath(u"plugins"_s);

    // Installed: <exe>/ai. Build tree: the source checkout recorded at configure time.
    const QString sourceAi = QDir(QStringLiteral(MIKSERSTUDIO_SOURCE_DIR)).filePath(u"ai"_s);
    const QString installedAi = appDir.filePath(u"ai"_s);
    if (const QString overridden = qEnvironmentVariable("MIKSERSTUDIO_AI_DIR"); !overridden.isEmpty()) {
        paths.aiDir = QDir(overridden).absolutePath();
    } else if (isAiProject(installedAi)) {
        paths.aiDir = installedAi;
    } else if (isAiProject(sourceAi)) {
        paths.aiDir = sourceAi;
    }
    // The AI service runs ffmpeg; one shipped with Mikser Studio wins over any on PATH.
    if (const QFileInfo ffmpeg(appDir.filePath(u"ffmpeg"_s + exeSuffix())); ffmpeg.isFile())
        paths.ffmpegPath = ffmpeg.absoluteFilePath();
    return paths;
}

}  // namespace mikserstudio::app
