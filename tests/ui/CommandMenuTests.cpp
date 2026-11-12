#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/PluginManager.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/core/UndoStack.h"
#include "mikserstudio/ui/KddwDockHost.h"
#include "mikserstudio/ui/PanelIds.h"
#include "mikserstudio/ui/Shell.h"

#include "CommandMenus.h"
#include "TestCommands.h"

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QJsonDocument>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;
using mikserstudio::ui::CommandMenus;
using mikserstudio::ui::test::registerTestCommand;

namespace {

QJsonObject requiredNameSchema()
{
    return QJsonDocument::fromJson(R"({"type": "object", "required": ["name"],
                                       "properties": {"name": {"type": "string"}}})")
        .object();
}

QStringList actionTexts(const QMenu* menu)
{
    QStringList texts;
    for (const QAction* action : menu->actions())
        texts.append(action->text());
    return texts;
}

QStringList menuBarTitles(const QMenuBar& bar)
{
    QStringList titles;
    for (const QAction* action : bar.actions())
        titles.append(action->text());
    return titles;
}

struct MenuFixture {
    mikserstudio::core::UndoStack undo;
    mikserstudio::core::CommandBus bus{undo};
    QWidget window;
    QMenuBar menuBar;
    int runs = 0;

    MenuFixture()
    {
        REQUIRE(registerTestCommand(bus,
                                    {.id = u"timeline.split"_s,
                                     .title = u"Split Clip"_s,
                                     .category = u"Timeline"_s,
                                     .shortcut = u"S"_s,
                                     .editText = u"Split Clip"_s},
                                    &runs));
        REQUIRE(registerTestCommand(bus, {.id = u"project.rename"_s,
                                          .title = u"Rename Project"_s,
                                          .category = u"Project"_s,
                                          .paramsSchema = requiredNameSchema()}));
        REQUIRE(registerTestCommand(bus, {.id = u"ai.caption"_s, .title = u"Caption Clip"_s, .category = u"AI"_s}));
        REQUIRE(registerTestCommand(
            bus,
            {.id = u"file.new_project"_s, .title = u"New Project"_s, .category = u"File"_s, .shortcut = u"Ctrl+N"_s}));
    }
};

}  // namespace

TEST_CASE("CommandMenus builds one menu per category in a fixed order", "[menus]")
{
    MenuFixture f;
    CommandMenus menus(f.bus, &f.menuBar, &f.window);

    CHECK(menus.categories() == QStringList{u"File"_s, u"Project"_s, u"Edit"_s, u"Timeline"_s, u"AI"_s});
    CHECK(menuBarTitles(f.menuBar) == menus.categories());

    QAction* split = menus.action(u"timeline.split"_s);
    REQUIRE(split);
    CHECK(split->text() == u"Split Clip"_s);
    CHECK(split->shortcut() == QKeySequence(u"S"_s));
    CHECK(split->shortcutContext() == Qt::ApplicationShortcut);
    CHECK(split->toolTip() == u"Split Clip (test)."_s);
    CHECK(menus.menu(u"Timeline"_s)->actions().contains(split));

    // Commands that need params open a dialog first.
    CHECK(menus.action(u"project.rename"_s)->text() == u"Rename Project…"_s);
    CHECK(menus.action(u"file.new_project"_s)->text() == u"New Project"_s);

    split->trigger();
    CHECK(f.runs == 1);
}

TEST_CASE("CommandMenus shows what Undo and Redo would revert", "[menus]")
{
    MenuFixture f;
    CommandMenus menus(f.bus, &f.menuBar, &f.window);
    QMenu* edit = menus.menu(u"Edit"_s);
    REQUIRE(edit);
    REQUIRE(edit->actions().size() >= 2);
    CHECK(edit->actions().at(0) == menus.action(u"edit.undo"_s));
    CHECK(edit->actions().at(1) == menus.action(u"edit.redo"_s));

    QAction* undo = menus.action(u"edit.undo"_s);
    QAction* redo = menus.action(u"edit.redo"_s);
    CHECK(undo->text() == u"Undo"_s);
    CHECK_FALSE(undo->isEnabled());
    CHECK_FALSE(redo->isEnabled());

    REQUIRE(f.bus.execute(u"timeline.split"_s));
    CHECK(undo->text() == u"Undo Split Clip"_s);
    CHECK(undo->isEnabled());

    undo->trigger();
    CHECK(undo->text() == u"Undo"_s);
    CHECK(redo->text() == u"Redo Split Clip"_s);
    CHECK(redo->isEnabled());
}

TEST_CASE("CommandMenus follows commands registered and unregistered later", "[menus]")
{
    MenuFixture f;
    QMenu* workspace = f.menuBar.addMenu(u"Workspace"_s);
    CommandMenus menus(f.bus, &f.menuBar, &f.window);
    menus.setInsertBefore(workspace->menuAction());
    CHECK(menuBarTitles(f.menuBar).last() == u"Workspace"_s);

    REQUIRE(registerTestCommand(f.bus,
                                {.id = u"color.auto_balance"_s, .title = u"Auto Balance"_s, .category = u"Color"_s}));
    REQUIRE(
        registerTestCommand(f.bus, {.id = u"timeline.join"_s, .title = u"Join Clips"_s, .category = u"Timeline"_s}));
    CHECK(menus.categories() == QStringList{u"File"_s, u"Project"_s, u"Edit"_s, u"Timeline"_s, u"AI"_s, u"Color"_s});
    CHECK(menuBarTitles(f.menuBar) ==
          QStringList{u"File"_s, u"Project"_s, u"Edit"_s, u"Timeline"_s, u"AI"_s, u"Color"_s, u"Workspace"_s});
    // Sorted by title inside the menu.
    CHECK(actionTexts(menus.menu(u"Timeline"_s)) == QStringList{u"Join Clips"_s, u"Split Clip"_s});

    f.bus.unregisterCommand(u"timeline.join"_s);
    CHECK(menus.action(u"timeline.join"_s) == nullptr);
    CHECK(actionTexts(menus.menu(u"Timeline"_s)) == QStringList{u"Split Clip"_s});

    // The last command of a category takes its menu with it.
    f.bus.unregisterCommand(u"color.auto_balance"_s);
    CHECK(menus.menu(u"Color"_s) == nullptr);
    CHECK_FALSE(menuBarTitles(f.menuBar).contains(u"Color"_s));
}

TEST_CASE("Shell generates menus from the bus and lists panels in the Window menu", "[menus][shell]")
{
    QTemporaryDir dir;
    mikserstudio::core::UndoStack undo;
    mikserstudio::core::CommandBus bus(undo);
    mikserstudio::core::ProjectModel project;
    mikserstudio::core::PluginManager plugins(bus, dir.filePath(u"plugins"_s));
    REQUIRE(registerTestCommand(bus, {.id = u"timeline.split"_s, .title = u"Split Clip"_s, .category = u"Timeline"_s}));

    mikserstudio::ui::Shell shell(bus, project, plugins, dir.filePath(u"config"_s));
    CHECK(shell.commandMenuCategories() == QStringList{u"Edit"_s, u"Timeline"_s});
    CHECK(shell.commandAction(u"timeline.split"_s));

    const QStringList bar = menuBarTitles(*shell.mainWindow()->menuBar());
    REQUIRE(bar.size() >= 4);
    CHECK(bar.mid(0, 2) == QStringList{u"Edit"_s, u"Timeline"_s});
    CHECK(bar.indexOf(shell.workspaceMenu()->menuAction()->text()) > 1);
    CHECK(bar.indexOf(shell.windowMenu()->menuAction()->text()) > 1);

    REQUIRE(
        registerTestCommand(bus, {.id = u"render.preview"_s, .title = u"Render Preview"_s, .category = u"Render"_s}));
    CHECK(shell.commandMenuCategories().contains(u"Render"_s));
    CHECK(shell.commandMenu(u"Render"_s)->actions().contains(shell.commandAction(u"render.preview"_s)));
    bus.unregisterCommand(u"render.preview"_s);
    CHECK_FALSE(shell.commandMenuCategories().contains(u"Render"_s));

    // Every core panel is in the Window menu, checked while open.
    for (const QStringView id : {mikserstudio::ui::panel_ids::kMediaBin, mikserstudio::ui::panel_ids::kTimeline,
                                 mikserstudio::ui::panel_ids::kProperties, mikserstudio::ui::panel_ids::kAi,
                                 mikserstudio::ui::panel_ids::kLog, mikserstudio::ui::panel_ids::kPlugins}) {
        QAction* action = shell.windowMenuAction(id.toString());
        INFO(id.toString().toStdString());
        REQUIRE(action);
        CHECK(action->isCheckable());
    }
    QAction* log = shell.windowMenuAction(mikserstudio::ui::panel_ids::kLog.toString());
    log->trigger();
    CHECK(shell.dockHost().isPanelOpen(mikserstudio::ui::panel_ids::kLog.toString()) == log->isChecked());
}
