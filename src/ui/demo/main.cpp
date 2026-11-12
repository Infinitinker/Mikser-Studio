// mikser-studio-ui-demo: the Mikser Studio shell with fake media, a fake transport and a fake AI service, so
// docking, floating, workspaces, the timeline and the panels can be tried without the engine.
//
//   mikser-studio-ui-demo [--config <dir>] [--workspace <name>] [--screenshot <png> [--float <panel-id>]]

#include "DemoMonitorPanel.h"
#include "DemoProject.h"
#include "FakeAiService.h"
#include "FakeMediaProbe.h"
#include "FakeTransport.h"
#include "Screenshot.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/PluginManager.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/core/ProjectCommands.h"
#include "mikserstudio/core/Transport.h"
#include "mikserstudio/core/UndoStack.h"
#include "mikserstudio/ui/ClipSelection.h"
#include "mikserstudio/ui/KddwDockHost.h"
#include "mikserstudio/ui/PanelIds.h"
#include "mikserstudio/ui/Shell.h"
#include "mikserstudio/ui/Theme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QMainWindow>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>

using namespace Qt::StringLiterals;
using namespace mikserstudio;

namespace {

constexpr int kSettleMs = 1200;
constexpr int kFloatSettleMs = 900;

void registerMonitor(ui::KddwDockHost& host, QStringView id, const QString& title, core::Transport& transport,
                     const core::ProjectModel& project)
{
    host.registerPanel(
        {id.toString(), title, ui::DockArea::Center, true, [title, &transport, &project](QWidget* parent) {
             return new ui::demo::DemoMonitorPanel(title, transport, project, parent);
         }});
}

void takeScreenshot(ui::Shell& shell, const QString& path, const QString& floatPanel)
{
    ui::KddwDockHost& host = shell.dockHost();
    QMainWindow* window = shell.mainWindow();
    if (!floatPanel.isEmpty()) {
        if (!host.isPanelOpen(floatPanel))
            (void)host.openPanel(floatPanel);
        if (const auto floated = host.setPanelFloating(floatPanel, true); !floated)
            std::fprintf(stderr, "Could not float %s: %s\n", qPrintable(floatPanel), qPrintable(floated.error()));
        if (QWidget* floating = host.panelWindow(floatPanel); floating && floating != window) {
            const QRect main = window->geometry();
            floating->resize(400, 470);
            floating->move(main.right() - 400 - 330, main.top() + 150);
        }
    }
    QTimer::singleShot(kFloatSettleMs, window, [&shell, &host, path, floatPanel] {
        QList<QWidget*> floating;
        if (QWidget* panelWindow = host.panelWindow(floatPanel))
            floating.append(panelWindow);
        const QImage image = ui::demo::composeScreenshot(shell.mainWindow(), floating);
        const bool saved = image.save(path);
        std::fprintf(saved ? stdout : stderr, "%s %s (%dx%d)\n", saved ? "Saved" : "Could not save", qPrintable(path),
                     image.width(), image.height());
        QApplication::exit(saved ? 0 : 1);
    });
}

}  // namespace

int main(int argc, char** argv)
{
    ui::prepareApplication();
    QApplication app(argc, argv);
    QApplication::setApplicationName(u"mikser-studio-ui-demo"_s);
    QApplication::setOrganizationName(u"Mikser Studio"_s);
    ui::applyTheme(app);

    QCommandLineParser parser;
    parser.setApplicationDescription(u"Mikser Studio UI demo with fake media, transport and AI."_s);
    parser.addHelpOption();
    const QCommandLineOption configOption(u"config"_s, u"Folder for workspaces, settings and demo files."_s, u"dir"_s);
    const QCommandLineOption workspaceOption(u"workspace"_s, u"Workspace to load after start."_s, u"name"_s);
    const QCommandLineOption screenshotOption(u"screenshot"_s, u"Save a screenshot to <png> and quit."_s, u"png"_s);
    const QCommandLineOption floatOption(u"float"_s, u"Panel to float before the screenshot."_s, u"panel-id"_s,
                                         ui::panel_ids::kAi.toString());
    parser.addOptions({configOption, workspaceOption, screenshotOption, floatOption});
    parser.process(app);

    const QString configDir = parser.isSet(configOption)
                                  ? QDir(parser.value(configOption)).absolutePath()
                                  : QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);

    core::UndoStack undo;
    core::CommandBus bus(undo);
    core::ProjectModel project;
    ui::demo::FakeMediaProbe probe;
    core::registerProjectCommands(bus, project, probe);

    ui::demo::FakeTransport program(project.frameRate());
    ui::demo::FakeTransport source(project.frameRate());
    source.setDuration(static_cast<core::FramePos>(project.frameRate().toDouble() * 20));
    core::registerTransportCommands(bus, program, u"program"_s, u"Program monitor"_s);
    core::registerTransportCommands(bus, source, u"source"_s, u"Source monitor"_s);
    QObject::connect(&project, &core::ProjectModel::timelineChanged, &program,
                     [&] { program.setDuration(project.duration()); });
    QObject::connect(&project, &core::ProjectModel::projectReset, &program, [&] {
        program.setFrameRate(project.frameRate());
        program.setDuration(project.duration());
    });

    ui::demo::FakeAiService ai(bus, project);

    core::PluginManager plugins(bus, QDir(configDir).filePath(u"plugins"_s));
    plugins.addSearchPath(QDir(QCoreApplication::applicationDirPath()).filePath(u"plugins"_s));
    plugins.discover();
    plugins.activateAll();

    ui::Shell shell(bus, project, plugins, configDir);
    registerMonitor(shell.dockHost(), ui::panel_ids::kSourceMonitor, u"Source monitor"_s, source, project);
    registerMonitor(shell.dockHost(), ui::panel_ids::kProgramMonitor, u"Program monitor"_s, program, project);
    shell.setProgramTransport(&program);

    const auto clips = ui::demo::populateDemoProject(bus, project, QDir(configDir).filePath(u"demo-media"_s));
    if (!clips) {
        std::fprintf(stderr, "Could not build the demo project: %s\n", qPrintable(clips.error()));
        return 1;
    }
    undo.clear();
    ai.submit(u"text.generate"_s, u"Generate text: shot ideas for the opening"_s, 1.0);
    ai.submit(u"video.upscale"_s, u"Upscale “Interview A” x2"_s, 0.35);
    program.seek(static_cast<core::FramePos>(project.frameRate().toDouble() * 11.5));
    if (clips->size() > 1)
        shell.selection().select(clips->at(1));

    shell.restoreState();
    shell.show();
    if (parser.isSet(workspaceOption)) {
        if (const auto loaded = shell.dockHost().loadWorkspace(parser.value(workspaceOption)); !loaded)
            std::fprintf(stderr, "%s\n", qPrintable(loaded.error()));
    }

    if (parser.isSet(screenshotOption)) {
        const QString path = QDir(parser.value(screenshotOption)).absolutePath();
        const QString floatPanel = parser.value(floatOption);
        QTimer::singleShot(kSettleMs, shell.mainWindow(),
                           [&shell, path, floatPanel] { takeScreenshot(shell, path, floatPanel); });
    }
    return QApplication::exec();
}
