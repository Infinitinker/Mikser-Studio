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

TEST_CASE("The default command runs mikserstudio-ai from the ai folder's environment", "[services][sidecar]")
{
    QTemporaryDir root;
    const QString ai = root.filePath(u"ai"_s);
#ifdef Q_OS_WIN
    const QString bin = QDir(ai).filePath(u".venv/Scripts"_s);
    const QString programName = u"mikserstudio-ai.exe"_s;
#else
    const QString bin = QDir(ai).filePath(u".venv/bin"_s);
    const QString programName = u"mikserstudio-ai"_s;
#endif
    QDir().mkpath(ai);

    SidecarOptions options;
    options.cacheDirectory = root.filePath(u"cache"_s);
    options.aiDirectory = ai;

    SECTION("without the ai folder")
    {
        const auto launch = resolveSidecarLaunch(options, "token");
        REQUIRE_FALSE(launch);
        CHECK(launch.error().contains(u"was not found. Reinstall Mikser Studio."_s));
    }

    QFile pyproject(QDir(ai).filePath(u"pyproject.toml"_s));
    REQUIRE(pyproject.open(QIODevice::WriteOnly));
    pyproject.close();

    SECTION("without its environment")
    {
        const auto launch = resolveSidecarLaunch(options, "token");
        REQUIRE_FALSE(launch);
        CHECK(launch.error() == u"The AI service could not start: its Python environment \"%1\" was not found. Create it as docs/development.md describes and restart Mikser Studio."_s
                                    .arg(QDir::toNativeSeparators(QDir(ai).filePath(u".venv"_s))));
    }

    SECTION("with its environment")
    {
        QDir().mkpath(bin);
        QFile program(QDir(bin).filePath(programName));
        REQUIRE(program.open(QIODevice::WriteOnly));
        program.close();
        program.setPermissions(program.permissions() | QFileDevice::ExeOwner);
        options.ffmpegPath = u"C:/tools/ffmpeg.exe"_s;
        const auto launch = resolveSidecarLaunch(options, "secret-token");
        REQUIRE(launch);
        CHECK(QDir::cleanPath(launch->program) == QDir::cleanPath(QDir(bin).filePath(programName)));
        CHECK(launch->arguments == QStringList{
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
