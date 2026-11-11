#include "ServicesTestSupport.h"
#include "SidecarLaunch.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;
using namespace mikserstudio::services;

TEST_CASE("The READY line gives the port", "[services][sidecar]")
{
    CHECK(parseReadyLine("MIKSERSTUDIO_AI_READY port=51234") == 51234);
    CHECK(parseReadyLine("MIKSERSTUDIO_AI_READY port=51234\r\n") == 51234);
    CHECK(parseReadyLine("MIKSERSTUDIO_AI_READY port=0") == -1);
    CHECK(parseReadyLine("MIKSERSTUDIO_AI_READY port=70000") == -1);
    CHECK(parseReadyLine("MIKSERSTUDIO_AI_READY port=abc") == -1);
    CHECK(parseReadyLine("Installed 3 packages") == -1);
}

TEST_CASE("Tokens are 43 base64url characters and differ every time", "[services][sidecar]")
{
    const QByteArray first = newSidecarToken();
    CHECK(first.size() == 43);
    CHECK(first != newSidecarToken());
    for (char c : first)
        CHECK((std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'));
}

TEST_CASE("A missing uv is reported with what to install", "[services][sidecar]")
{
    QTemporaryDir cache;
    QProcessEnvironment environment;
    environment.insert(u"PATH"_s, cache.path());
    SidecarOptions options;
    options.cacheDirectory = cache.path();
    options.environment = environment;
    const auto launch = resolveSidecarLaunch(options, "token");
    REQUIRE_FALSE(launch);
    CHECK(launch.error() ==
          u"The AI service could not start: uv was not found. Install uv from https://docs.astral.sh/uv/ and restart Mikser Studio."_s);
}

TEST_CASE("The default command runs mikserstudio-ai through uv in the ai folder", "[services][sidecar]")
{
    QTemporaryDir root;
    const QString bin = root.filePath(u"bin"_s);
    const QString ai = root.filePath(u"ai"_s);
    QDir().mkpath(bin);
    QDir().mkpath(ai);
#ifdef Q_OS_WIN
    const QString uvName = u"uv.exe"_s;
#else
    const QString uvName = u"uv"_s;
#endif
    QFile uv(QDir(bin).filePath(uvName));
    REQUIRE(uv.open(QIODevice::WriteOnly));
    uv.close();
    uv.setPermissions(uv.permissions() | QFileDevice::ExeOwner);

    SidecarOptions options;
    options.cacheDirectory = root.filePath(u"cache"_s);
    options.environment.insert(u"PATH"_s, bin);

    SECTION("without the ai folder")
    {
        options.aiDirectory = ai;
        const auto launch = resolveSidecarLaunch(options, "token");
        REQUIRE_FALSE(launch);
        CHECK(launch.error().contains(u"was not found. Reinstall Mikser Studio."_s));
    }

    SECTION("with the ai folder")
    {
        QFile pyproject(QDir(ai).filePath(u"pyproject.toml"_s));
        REQUIRE(pyproject.open(QIODevice::WriteOnly));
        pyproject.close();
        options.aiDirectory = ai;
        options.ffmpegPath = u"C:/tools/ffmpeg.exe"_s;
        const auto launch = resolveSidecarLaunch(options, "secret-token");
        REQUIRE(launch);
        CHECK(QDir::cleanPath(launch->program) == QDir::cleanPath(QDir(bin).filePath(uvName)));
        CHECK(launch->arguments.mid(0, 5) ==
              QStringList{u"run"_s, u"--project"_s, QDir::toNativeSeparators(ai), u"--no-dev"_s, u"mikserstudio-ai"_s});
        CHECK(launch->arguments.mid(5) == QStringList{
                                              u"--host"_s,
                                              u"127.0.0.1"_s,
                                              u"--port"_s,
                                              u"0"_s,
                                              u"--cache-dir"_s,
                                              QDir::toNativeSeparators(options.cacheDirectory),
                                              u"--exit-with-stdin"_s,
                                              u"--ffmpeg"_s,
                                              QDir::toNativeSeparators(u"C:/tools/ffmpeg.exe"_s),
                                          });
        CHECK(launch->environment.value(u"MIKSERSTUDIO_AI_TOKEN"_s) == u"secret-token"_s);
        CHECK(launch->environment.value(u"NoDefaultCurrentDirectoryInExePath"_s) == u"1"_s);
        CHECK(QDir(launch->workingDirectory) == QDir(options.cacheDirectory));
        CHECK(QDir::isAbsolutePath(launch->workingDirectory));
        CHECK_FALSE(launch->arguments.join(u' ').contains(u"secret-token"_s));
        CHECK(QDir(options.cacheDirectory).exists());
    }
}

TEST_CASE("A custom program keeps its arguments and gets the sidecar ones", "[services][sidecar]")
{
    QTemporaryDir cache;
    SidecarOptions options;
    options.program = u"python"_s;
    options.arguments = {u"-m"_s, u"mikserstudio_ai"_s};
    options.cacheDirectory = cache.path();
    const auto launch = resolveSidecarLaunch(options, "t");
    REQUIRE(launch);
    CHECK(launch->program == u"python"_s);
    CHECK(launch->arguments.mid(0, 3) == QStringList{u"-m"_s, u"mikserstudio_ai"_s, u"--host"_s});
    CHECK(launch->arguments.last() == u"--exit-with-stdin"_s);
    CHECK(QDir(launch->workingDirectory) == QDir(cache.path()));
}

TEST_CASE("A cache folder is required", "[services][sidecar]")
{
    SidecarOptions options;
    options.program = u"python"_s;
    CHECK(resolveSidecarLaunch(options, "t").error() ==
          u"The AI service could not start: no cache folder was configured."_s);
}
