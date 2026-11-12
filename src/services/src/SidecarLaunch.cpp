#include "SidecarLaunch.h"

#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>

#include <array>

using namespace Qt::StringLiterals;

namespace mikserstudio::services {

namespace {

constexpr QByteArrayView kReadyPrefix = "MIKSERSTUDIO_AI_READY port=";

// The mikserstudio-ai program pip installs into the ai/ folder's virtual environment.
QString environmentProgram(const QDir& ai)
{
#ifdef Q_OS_WIN
    return ai.filePath(u".venv/Scripts/mikserstudio-ai.exe"_s);
#else
    return ai.filePath(u".venv/bin/mikserstudio-ai"_s);
#endif
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
        const QFileInfo project(QDir(options.aiDirectory).filePath(u"pyproject.toml"_s));
        if (options.aiDirectory.isEmpty() || !project.isFile()) {
            return std::unexpected(u"The AI service could not start: its folder \"%1\" was not found. Reinstall Mikser Studio."_s
                                       .arg(QDir::toNativeSeparators(options.aiDirectory)));
        }
        const QDir ai(project.absolutePath());
        const QFileInfo program(environmentProgram(ai));
        if (!program.isFile()) {
            return std::unexpected(u"The AI service could not start: its Python environment \"%1\" was not found. Create it as docs/development.md describes and restart Mikser Studio."_s
                                       .arg(QDir::toNativeSeparators(ai.filePath(u".venv"_s))));
        }
        launch.program = program.absoluteFilePath();
        launch.arguments = {};
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
