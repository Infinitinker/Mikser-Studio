#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/PluginManager.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/core/UndoStack.h"
#include "mikserstudio/ui/KddwDockHost.h"
#include "mikserstudio/ui/PanelIds.h"
#include "mikserstudio/ui/Shell.h"

#include <catch2/catch_test_macros.hpp>

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
        mikserstudio::ui::KddwDockHost& host = shell.dockHost();
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
        mikserstudio::ui::KddwDockHost& host = shell.dockHost();
        CHECK(host.isPanelOpen(panel_ids::kTimeline.toString()));
        CHECK_FALSE(host.isPanelOpen(panel_ids::kMediaBin.toString()));
        CHECK(host.currentWorkspace() == u"Editing"_s);
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
