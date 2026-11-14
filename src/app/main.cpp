// mikser-studio: Mikser Studio.
//
//   mikser-studio [files...] [--play] [--screenshot <png> [--screenshot-delay <ms>]]
//   mikser-studio --headless [--api-port <port>]
//   mikser-studio [--no-api] [--api-port <port>] [--safe-mode]
//   mikser-studio --print-lua-types <file>
//
// MIKSERSTUDIO_PLUGIN_PATH names folders of plugins in development; MIKSERSTUDIO_PLUGIN_HOT_RELOAD=1
// reloads a plugin when its files change, which a build from source does anyway.

#include "AppPaths.h"
#include "DesktopApp.h"
#include "FatalError.h"
#include "LogFile.h"
#include "Logging.h"
#include "ShutdownSignals.h"
#include "Studio.h"

#include "mikserstudio/core/Instance.h"
#include "mikserstudio/lua/LuaTypes.h"
#include "mikserstudio/ui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>

#include <cstdio>
#include <cstring>
#include <expected>
#include <memory>

using namespace Qt::StringLiterals;
using namespace mikserstudio;

namespace {

struct CommandLine {
    bool headless = false;
    bool api = true;
    quint16 apiPort = 0;
    bool safeMode = false;
    app::DesktopOptions desktop;
};

// Where the option is on the command line; 0 when it isn't.
int findOption(int argc, char** argv, const char* option)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], option) == 0)
            return i;
    }
    return 0;
}

// Writes the types of the plugins' Lua API to the file after --print-lua-types (docs/plugins.md,
// "Tooling"). A file, not standard output, so the line ends are \n on every system. A program
// without a console may show no message, so the exit code also says whether it worked.
int printLuaTypes(int argc, char** argv, int at)
{
    if (at + 1 >= argc) {
        std::fputs("--print-lua-types needs the file to write, such as plugins/types/ms.lua.\n", stderr);
        return 2;
    }
    QFile file(QString::fromLocal8Bit(argv[at + 1]));
    const QByteArray types = lua::luaTypes().toUtf8();
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(types) != types.size()) {
        std::fprintf(stderr, "Can't write %s: %s\n", argv[at + 1], qUtf8Printable(file.errorString()));
        return 1;
    }
    return 0;
}

// The command line, or what is wrong with it. --help, --version and unknown options are answered by
// QCommandLineParser itself, which then exits.
std::expected<CommandLine, QString> parseCommandLine(const QCoreApplication& app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(MIKSERSTUDIO_DESCRIPTION));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(u"files"_s, u"Media files to import and place on the timeline."_s, u"[files...]"_s);
    const QCommandLineOption headless(u"headless"_s,
                                      u"Run without a window; control Mikser Studio through the local API."_s);
    const QCommandLineOption noApi(u"no-api"_s, u"Do not start the local API."_s);
    const QCommandLineOption apiPort(u"api-port"_s, u"Port of the local API (default: any free port)."_s, u"port"_s);
    const QCommandLineOption safeMode(u"safe-mode"_s, u"Run only the built-in plugins."_s);
    // Handled in main, before anything starts; here for --help.
    const QCommandLineOption luaTypes(
        u"print-lua-types"_s, u"Write the types of the plugins' Lua API, for Lua language servers, to <file>."_s,
        u"file"_s);
    const QCommandLineOption play(u"play"_s, u"Start playing the timeline after start-up."_s);
    const QCommandLineOption screenshot(u"screenshot"_s, u"Save a screenshot of the window to <png>, then quit."_s,
                                        u"png"_s);
    const QCommandLineOption screenshotDelay(u"screenshot-delay"_s, u"Milliseconds to wait before the screenshot."_s,
                                             u"ms"_s, u"4000"_s);
    parser.addOptions({headless, noApi, apiPort, safeMode, luaTypes, play, screenshot, screenshotDelay});
    parser.process(app);

    CommandLine line;
    line.headless = parser.isSet(headless);
    line.api = !parser.isSet(noApi);
    line.safeMode = parser.isSet(safeMode);
    if (parser.isSet(apiPort)) {
        bool ok = false;
        const uint port = parser.value(apiPort).toUInt(&ok);
        if (!ok || port == 0 || port > 65535) {
            return std::unexpected(
                u"--api-port is “%1”. Give it a port number from 1 to 65535."_s.arg(parser.value(apiPort)));
        }
        line.apiPort = static_cast<quint16>(port);
    }
    if (line.headless && !line.api) {
        return std::unexpected(
            u"--headless needs the local API, since nothing else could control Mikser Studio. Leave out --no-api."_s);
    }
    line.desktop.importFiles = parser.positionalArguments();
    line.desktop.play = parser.isSet(play);
    line.desktop.screenshotPath = parser.value(screenshot);
    line.desktop.screenshotDelayMs = parser.value(screenshotDelay).toInt();
    return line;
}

// Names the app after its instance (core/Instance.h). An unknown MIKSERSTUDIO_INSTANCE is an error,
// returned for the caller to show: a build without a console has nowhere else to show it.
std::expected<void, QString> setIdentity()
{
    const auto instance = core::currentInstance();
    if (!instance)
        return std::unexpected(instance.error());
    core::applyInstance(*instance);
    QCoreApplication::setApplicationVersion(QStringLiteral(MIKSERSTUDIO_VERSION));
    // Wayland matches windows to the .desktop file by this name; it must stay the app id forever.
    QGuiApplication::setDesktopFileName(instance->appId);
    return {};
}

// A plugin reloads when its files change in a build from source (Mikser Studio Dev), or when
// MIKSERSTUDIO_PLUGIN_HOT_RELOAD is 1.
bool pluginHotReload()
{
    const auto instance = core::currentInstance();
    const bool fromSource = instance && instance->appId == QLatin1StringView(core::kAppId) + u".dev"_s;
    return fromSource || qEnvironmentVariable("MIKSERSTUDIO_PLUGIN_HOT_RELOAD") == u"1"_s;
}

app::StudioOptions studioOptions(const CommandLine& line, bool headless)
{
    return {.audio = !headless,
            .api = headless || line.api,
            .apiPort = line.apiPort,
            .safeMode = line.safeMode,
            .pluginHotReload = pluginHotReload()};
}

// The log file in the instance's logs folder; without one, Mikser Studio runs on and says why. A
// command line that cannot run adds its reason to the current log rather than starting a new one, and
// --help, --version and unknown options stop before the log is opened: neither pushes out the log of
// a real run.
std::unique_ptr<app::LogFile> openLog(const app::AppPaths& paths, bool runs)
{
    auto log = app::LogFile::open(paths.user.logs,
                                  runs ? app::LogFile::Start::NewLog : app::LogFile::Start::ContinueLog);
    if (!log) {
        qCWarning(app::lcApp).noquote() << log.error();
        return nullptr;
    }
    qCInfo(app::lcApp).noquote() << QCoreApplication::applicationName() << QCoreApplication::applicationVersion()
                                 << "started; the log is" << QDir::toNativeSeparators((*log)->path());
    return std::move(*log);
}

int runHeadless(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (const auto identity = setIdentity(); !identity) {
        app::reportFatalError(identity.error(), app::StartedAs::Headless);
        return 2;
    }
    const app::AppPaths paths = app::AppPaths::resolve();
    const auto line = parseCommandLine(app);
    const auto log = openLog(paths, line.has_value());  // Outlives the studio, so its teardown is logged.
    if (!line) {
        app::reportFatalError(line.error(), app::StartedAs::Headless);
        return 2;
    }
    const app::ShutdownSignals shutdown;  // Outlives the studio, so Ctrl+C runs its teardown.
    app::Studio studio(paths, studioOptions(*line, true));
    studio.start();
    if (!studio.apiServer() || !studio.apiServer()->isRunning())
        return 1;
    qCInfo(app::lcApp) << "Running headless. Stop with Ctrl+C.";
    return QCoreApplication::exec();
}

int runDesktop(int argc, char** argv)
{
    ui::prepareApplication();
    QApplication app(argc, argv);
    if (const auto identity = setIdentity(); !identity) {
        app::reportFatalError(identity.error(), app::StartedAs::Desktop);
        return 2;
    }
    ui::applyTheme(app);
    QApplication::setWindowIcon(QIcon(u":/mikserstudio/icon.svg"_s));
    const app::AppPaths paths = app::AppPaths::resolve();
    const auto line = parseCommandLine(app);
    const auto log = openLog(paths, line.has_value());
    if (!line) {
        app::reportFatalError(line.error(), app::StartedAs::Desktop);
        return 2;
    }
    const app::ShutdownSignals shutdown;
    app::Studio studio(paths, studioOptions(*line, false));
    studio.start();
    app::DesktopApp desktop(studio, paths.user.config, line->desktop);
    desktop.show();
    return QApplication::exec();
}

}  // namespace

int main(int argc, char** argv)
{
    if (const int at = findOption(argc, argv, "--print-lua-types"))
        return printLuaTypes(argc, argv, at);
    return findOption(argc, argv, "--headless") != 0 ? runHeadless(argc, argv) : runDesktop(argc, argv);
}
