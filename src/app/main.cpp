// mikser-studio: Mikser Studio.
//
//   mikser-studio [files...] [--play] [--screenshot <png> [--screenshot-delay <ms>]]
//   mikser-studio --headless [--api-port <port>]
//   mikser-studio [--no-api] [--api-port <port>]

#include "AppPaths.h"
#include "DesktopApp.h"
#include "LogFile.h"
#include "Logging.h"
#include "ShutdownSignals.h"
#include "Studio.h"

#include "mikserstudio/core/Instance.h"
#include "mikserstudio/ui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QMessageBox>

#include <cstdio>

#include <cstring>
#include <expected>
#include <memory>
#include <optional>

using namespace Qt::StringLiterals;
using namespace mikserstudio;

namespace {

struct CommandLine {
    bool headless = false;
    bool api = true;
    quint16 apiPort = 0;
    app::DesktopOptions desktop;
};

bool wantsHeadless(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--headless") == 0)
            return true;
    }
    return false;
}

std::optional<CommandLine> parseCommandLine(const QCoreApplication& app)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(u"Mikser Studio, an AI-backed multimedia studio."_s);
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(u"files"_s, u"Media files to import and place on the timeline."_s, u"[files...]"_s);
    const QCommandLineOption headless(u"headless"_s,
                                      u"Run without a window; control Mikser Studio through the local API."_s);
    const QCommandLineOption noApi(u"no-api"_s, u"Do not start the local API."_s);
    const QCommandLineOption apiPort(u"api-port"_s, u"Port of the local API (default: any free port)."_s, u"port"_s);
    const QCommandLineOption play(u"play"_s, u"Start playing the timeline after start-up."_s);
    const QCommandLineOption screenshot(u"screenshot"_s, u"Save a screenshot of the window to <png>, then quit."_s,
                                        u"png"_s);
    const QCommandLineOption screenshotDelay(u"screenshot-delay"_s, u"Milliseconds to wait before the screenshot."_s,
                                             u"ms"_s, u"4000"_s);
    parser.addOptions({headless, noApi, apiPort, play, screenshot, screenshotDelay});
    parser.process(app);

    CommandLine line;
    line.headless = parser.isSet(headless);
    line.api = !parser.isSet(noApi);
    if (parser.isSet(apiPort)) {
        bool ok = false;
        const uint port = parser.value(apiPort).toUInt(&ok);
        if (!ok || port == 0 || port > 65535) {
            qCCritical(app::lcApp) << "--api-port needs a number from 1 to 65535.";
            return std::nullopt;
        }
        line.apiPort = static_cast<quint16>(port);
    }
    if (line.headless && !line.api) {
        qCCritical(app::lcApp) << "--headless needs the API: without it nothing could control Mikser Studio.";
        return std::nullopt;
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

// The log file in the instance's logs folder; without one, Mikser Studio runs on and says why.
std::unique_ptr<app::LogFile> openLog(const app::AppPaths& paths)
{
    auto log = app::LogFile::open(paths.user.logs);
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
        std::fprintf(stderr, "%s\n", qPrintable(identity.error()));
        return 2;
    }
    const auto line = parseCommandLine(app);
    if (!line)
        return 2;
    const app::AppPaths paths = app::AppPaths::resolve();
    const auto log = openLog(paths);  // Outlives the studio, so its teardown is logged.
    const app::ShutdownSignals shutdown;  // Outlives the studio, so Ctrl+C runs its teardown.
    app::Studio studio(paths, {.audio = false, .api = true, .apiPort = line->apiPort});
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
        std::fprintf(stderr, "%s\n", qPrintable(identity.error()));
        QMessageBox::critical(nullptr, u"Mikser Studio"_s, identity.error());
        return 2;
    }
    ui::applyTheme(app);
    QApplication::setWindowIcon(QIcon(u":/mikserstudio/icon.svg"_s));
    const auto line = parseCommandLine(app);
    if (!line)
        return 2;

    const app::AppPaths paths = app::AppPaths::resolve();
    const auto log = openLog(paths);
    const app::ShutdownSignals shutdown;
    app::Studio studio(paths, {.audio = true, .api = line->api, .apiPort = line->apiPort});
    studio.start();
    app::DesktopApp desktop(studio, paths.user.config, line->desktop);
    desktop.show();
    return QApplication::exec();
}

}  // namespace

int main(int argc, char** argv)
{
    return wantsHeadless(argc, argv) ? runHeadless(argc, argv) : runDesktop(argc, argv);
}
