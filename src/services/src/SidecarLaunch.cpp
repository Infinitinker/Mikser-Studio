#include "SidecarLaunch.h"

#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QStandardPaths>

#include <array>

using namespace Qt::StringLiterals;

namespace mikserstudio::services {

namespace {

constexpr QByteArrayView kReadyPrefix = "MIKSERSTUDIO_AI_READY port=";

QStringList searchPath(const QProcessEnvironment& environment)
{
    return environment.value(u"PATH"_s).split(QDir::listSeparator(), Qt::SkipEmptyParts);
}

std::expected<QString, QString> findUv(const QProcessEnvironment& environment)
{
    const QStringList paths = searchPath(environment);
    // findExecutable() with an empty list would search the app's own PATH instead.
    const QString uv = paths.isEmpty() ? QString() : QStandardPaths::findExecutable(u"uv"_s, paths);
    if (uv.isEmpty()) {
        return std::unexpected(
            u"The AI service could not start: uv was not found. Install uv from https://docs.astral.sh/uv/ and restart Mikser Studio."_s);
    }
    return uv;
}

}  // namespace

std::expected<SidecarLaunch, QString> resolveSidecarLaunch(const SidecarOptions& options, const QByteArray& token)
{
    if (options.cacheDirectory.isEmpty())
        return std::unexpected(u"The AI service could not start: no cache folder was configured."_s);
    if (!QDir().mkpath(options.cacheDirectory)) {
        return std::unexpected(u"The AI service could not start: the cache folder \"%1\" cannot be created. Check that the drive is writable."_s
                                   .arg(QDir::toNativeSeparators(options.cacheDirectory)));
    }

    SidecarLaunch launch{
        .program = options.program,
        .arguments = options.arguments,
        .environment = options.environment,
        .workingDirectory = QFileInfo(options.cacheDirectory).absoluteFilePath(),
    };
    if (launch.program.isEmpty()) {
        auto uv = findUv(options.environment);
        if (!uv)
            return std::unexpected(uv.error());
        const QFileInfo project(QDir(options.aiDirectory).filePath(u"pyproject.toml"_s));
        if (options.aiDirectory.isEmpty() || !project.isFile()) {
            return std::unexpected(u"The AI service could not start: its folder \"%1\" was not found. Reinstall Mikser Studio."_s
                                       .arg(QDir::toNativeSeparators(options.aiDirectory)));
        }
        launch.program = *uv;
        launch.arguments = {u"run"_s, u"--project"_s, QDir::toNativeSeparators(project.absolutePath()), u"--no-dev"_s,
                            u"mikserstudio-ai"_s};
    }

    launch.arguments << u"--host"_s << u"127.0.0.1"_s << u"--port"_s << u"0"_s << u"--cache-dir"_s
                     << QDir::toNativeSeparators(launch.workingDirectory) << u"--exit-with-stdin"_s;
    if (!options.ffmpegPath.isEmpty())
        launch.arguments << u"--ffmpeg"_s << QDir::toNativeSeparators(options.ffmpegPath);

    launch.environment.insert(QString(kTokenVariable), QString::fromLatin1(token));
    launch.environment.insert(u"PYTHONUNBUFFERED"_s, u"1"_s);
    launch.environment.insert(u"PYTHONIOENCODING"_s, u"utf-8"_s);
    // Windows: programs the sidecar runs by name (ffmpeg) are not looked up in its working directory.
    launch.environment.insert(u"NoDefaultCurrentDirectoryInExePath"_s, u"1"_s);
    return launch;
}

QByteArray newSidecarToken()
{
    std::array<quint32, 8> words{};
    QRandomGenerator::system()->fillRange(words.data(), words.size());
    return QByteArray(reinterpret_cast<const char*>(words.data()), sizeof(words))
        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

int parseReadyLine(QByteArrayView line)
{
    line = line.trimmed();
    if (!line.startsWith(kReadyPrefix))
        return -1;
    bool ok = false;
    const int port = line.sliced(kReadyPrefix.size()).toInt(&ok);
    return ok && port > 0 && port <= 65535 ? port : -1;
}

}  // namespace mikserstudio::services
