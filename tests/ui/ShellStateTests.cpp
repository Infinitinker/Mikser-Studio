// The shell's saved state: it starts in the Editing workspace and keeps the layout and the last used
// workspace across sessions. Its title names the project and shows unsaved changes.
#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/PluginManager.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/core/UndoStack.h"
#include "mikserstudio/ui/DockHost.h"
#include "mikserstudio/ui/PanelIds.h"
#include "mikserstudio/ui/Shell.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMainWindow>
#include <QStatusBar>
#include <QTemporaryDir>

#include <algorithm>

using namespace Qt::StringLiterals;
using mikserstudio::ui::Shell;
namespace panel_ids = mikserstudio::ui::panel_ids;

namespace {

struct Core {
    explicit Core(const QString& dir)
        : plugins(bus, QDir(dir).filePath(u"plugins"_s))
    {
    }

    mikserstudio::core::UndoStack undo;
    mikserstudio::core::CommandBus bus{undo};
    mikserstudio::core::ProjectModel project;
    mikserstudio::core::PluginManager plugins;
};

bool statusBarShows(const Shell& shell, const QString& text)
{
    const auto labels = shell.mainWindow()->statusBar()->findChildren<QLabel*>();
    return std::ranges::any_of(labels, [&text](const QLabel* label) { return label->text() == text; });
}

}  // namespace

TEST_CASE("Shell starts in the Editing workspace and keeps the layout across sessions", "[shell]")
{
    QTemporaryDir dir;
    const QString configDir = dir.filePath(u"config"_s);
    Core core(dir.path());

    {
        Shell shell(core.bus, core.project, core.plugins, configDir);
        shell.restoreState();
        shell.show();
        mikserstudio::ui::DockHost& host = shell.dockHost();
        CHECK(host.currentWorkspace() == u"Editing"_s);
        CHECK(host.isPanelOpen(panel_ids::kTimeline.toString()));
        CHECK(host.isPanelOpen(panel_ids::kMediaBin.toString()));

        host.closePanel(panel_ids::kMediaBin.toString());
        REQUIRE_FALSE(host.isPanelOpen(panel_ids::kMediaBin.toString()));
        shell.mainWindow()->close();  // Closing the window saves the layout.
    }
    CHECK(QFile::exists(QDir(configDir).filePath(u"layout.json"_s)));
    CHECK(QFile::exists(QDir(configDir).filePath(u"shell.json"_s)));

    {
        Shell shell(core.bus, core.project, core.plugins, configDir);
        shell.restoreState();
        shell.show();
        mikserstudio::ui::DockHost& host = shell.dockHost();
        CHECK(host.isPanelOpen(panel_ids::kTimeline.toString()));
        CHECK_FALSE(host.isPanelOpen(panel_ids::kMediaBin.toString()));
        // The Editing workspace's background tabs (AI and Log behind Properties) come back too.
        CHECK(host.isPanelOpen(panel_ids::kPlugins.toString()));
        CHECK(host.isPanelOpen(panel_ids::kAi.toString()));
        CHECK(host.isPanelOpen(panel_ids::kLog.toString()));
        CHECK(host.currentWorkspace() == u"Editing"_s);
        // The first start filled the screen, and the restored window does too.
        CHECK(shell.mainWindow()->isMaximized());
        shell.mainWindow()->showNormal();
        shell.mainWindow()->close();
    }

    {
        Shell shell(core.bus, core.project, core.plugins, configDir);
        shell.restoreState();
        shell.show();
        CHECK_FALSE(shell.mainWindow()->isMaximized());  // The user made it smaller last time.
        // Closed while minimized from maximized (e.g. "Close window" on the taskbar button).
        shell.mainWindow()->showMaximized();
        shell.mainWindow()->showMinimized();
        shell.mainWindow()->close();
    }

    {
        Shell shell(core.bus, core.project, core.plugins, configDir);
        shell.restoreState();
        shell.show();
        CHECK(shell.mainWindow()->isMaximized());
        CHECK_FALSE(shell.mainWindow()->isMinimized());
        shell.mainWindow()->close();
    }
}

TEST_CASE("Shell restores the workspace that was last used", "[shell]")
{
    QTemporaryDir dir;
    const QString configDir = dir.filePath(u"config"_s);
    Core core(dir.path());

    {
        Shell shell(core.bus, core.project, core.plugins, configDir);
        shell.restoreState();
        shell.show();
        CHECK(statusBarShows(shell, u"Workspace: Editing"_s));
        // Loading on the host directly (not through the Workspace menu) updates the status bar too.
        REQUIRE(shell.dockHost().loadWorkspace(u"AI"_s));
        CHECK(statusBarShows(shell, u"Workspace: AI"_s));
        REQUIRE(shell.saveState());
    }
    // Without a saved layout the Shell applies the last workspace.
    REQUIRE(QFile::remove(QDir(configDir).filePath(u"layout.json"_s)));
    {
        Shell shell(core.bus, core.project, core.plugins, configDir);
        shell.restoreState();
        shell.show();
        CHECK(shell.dockHost().currentWorkspace() == u"AI"_s);
        CHECK(shell.dockHost().isPanelOpen(panel_ids::kAi.toString()));
    }
}

namespace {

// A change the person made, as far as the undo history can tell.
class SomeEdit final : public mikserstudio::core::UndoableEdit {
public:
    void undo() override { }
    void redo() override { }
    [[nodiscard]] QString text() const override { return u"Some edit"_s; }
};

}  // namespace

TEST_CASE("Shell titles the window after the project's file and marks unsaved changes", "[shell]")
{
    QTemporaryDir dir;
    Core core(dir.path());
    Shell shell(core.bus, core.project, core.plugins, dir.filePath(u"config"_s));
    QMainWindow* window = shell.mainWindow();
    const QString app = QCoreApplication::applicationName();

    CHECK(window->windowTitle() == u"Untitled[*] – %1"_s.arg(app));
    CHECK_FALSE(window->isWindowModified());

    core.undo.push(std::make_unique<SomeEdit>());
    CHECK(window->isWindowModified());

    core.project.setFilePath(u"D:/projects/Cut v1.2.msproj"_s);
    core.undo.setClean();
    CHECK(window->windowTitle() == u"Cut v1.2[*] – %1"_s.arg(app));
    CHECK_FALSE(window->isWindowModified());
}
