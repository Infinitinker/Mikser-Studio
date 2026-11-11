#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/PluginManager.h"
#include "mikserstudio/core/UndoStack.h"

#include "CommandInvoker.h"
#include "CommandLogModel.h"
#include "CommandMenus.h"
#include "CommandStatus.h"
#include "MessageBoxAnswer.h"
#include "PluginPanelBridge.h"
#include "QuietCommands.h"
#include "TestCommands.h"

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QLabel>
#include <QMenuBar>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;
using mikserstudio::ui::test::answerNextMessageBox;
using mikserstudio::ui::test::registerTestCommand;

namespace {

const auto kQuestion = u"Start a new project? Unsaved changes will be lost."_s;

struct BusFixture {
    mikserstudio::core::UndoStack undo;
    mikserstudio::core::CommandBus bus{undo};
    QWidget window;
    int guardedRuns = 0;
    int plainRuns = 0;

    BusFixture()
    {
        REQUIRE(registerTestCommand(bus,
                                    {.id = u"project.new"_s,
                                     .title = u"New Project"_s,
                                     .category = u"File"_s,
                                     .shortcut = u"Ctrl+N"_s,
                                     .confirmation = kQuestion},
                                    &guardedRuns));
        REQUIRE(registerTestCommand(bus, {.id = u"demo.plain"_s, .title = u"Plain"_s, .category = u"Demo"_s},
                                    &plainRuns));
    }
};

}  // namespace

TEST_CASE("Commands with a confirmation ask before an interactive run; Cancel runs nothing", "[invoker]")
{
    BusFixture f;

    QString shown;
    answerNextMessageBox(QMessageBox::RejectRole, &shown);
    CHECK_FALSE(mikserstudio::ui::invokeCommand(f.bus, u"project.new"_s, &f.window).has_value());
    CHECK(shown == kQuestion);
    CHECK(f.guardedRuns == 0);

    answerNextMessageBox(QMessageBox::AcceptRole);
    const auto accepted = mikserstudio::ui::invokeCommand(f.bus, u"project.new"_s, &f.window);
    REQUIRE(accepted.has_value());
    CHECK(accepted->has_value());
    CHECK(f.guardedRuns == 1);

    // Panels run commands with params through executeInteractive and executeReported.
    answerNextMessageBox(QMessageBox::RejectRole);
    CHECK_FALSE(mikserstudio::ui::executeInteractive(f.bus, u"project.new"_s, {}, &f.window).has_value());
    answerNextMessageBox(QMessageBox::RejectRole);
    mikserstudio::ui::executeReported(f.bus, u"project.new"_s, {}, &f.window);
    CHECK(f.guardedRuns == 1);

    // Without a confirmation nothing asks.
    CHECK(mikserstudio::ui::confirmCommand(f.bus, u"demo.plain"_s, &f.window));
    REQUIRE(mikserstudio::ui::invokeCommand(f.bus, u"demo.plain"_s, &f.window).has_value());
    CHECK(f.plainRuns == 1);
}

TEST_CASE("Menu actions and their shortcuts ask before running a command with a confirmation", "[invoker][menus]")
{
    BusFixture f;
    QMenuBar menuBar;
    mikserstudio::ui::CommandMenus menus(f.bus, &menuBar, &f.window);
    QAction* action = menus.action(u"project.new"_s);
    REQUIRE(action);
    CHECK(action->shortcut() == QKeySequence(u"Ctrl+N"_s));

    answerNextMessageBox(QMessageBox::RejectRole);
    action->trigger();
    CHECK(f.guardedRuns == 0);

    answerNextMessageBox(QMessageBox::AcceptRole);
    action->trigger();
    CHECK(f.guardedRuns == 1);
}

TEST_CASE("Plugin panels get a cancelled error when the user declines the confirmation", "[invoker][plugin-panels]")
{
    BusFixture f;
    QTemporaryDir dir;
    mikserstudio::core::PluginManager plugins(f.bus, dir.filePath(u"plugins"_s));
    mikserstudio::ui::PluginPanelBridge bridge(f.bus, plugins, u"com.example.demo"_s);

    answerNextMessageBox(QMessageBox::RejectRole);
    const QVariantMap cancelled = bridge.execute(u"project.new"_s);
    CHECK(cancelled.value(u"ok"_s) == false);
    CHECK(cancelled.value(u"error"_s).toMap().value(u"code"_s) == u"cancelled"_s);
    CHECK(f.guardedRuns == 0);

    answerNextMessageBox(QMessageBox::AcceptRole);
    CHECK(bridge.execute(u"project.new"_s).value(u"ok"_s) == true);
    CHECK(f.guardedRuns == 1);
}

TEST_CASE("Read-only commands stay out of the command log and the status bar", "[invoker][log]")
{
    mikserstudio::core::UndoStack undo;
    mikserstudio::core::CommandBus bus(undo);
    REQUIRE(registerTestCommand(bus, {.id = u"ai.jobs"_s, .title = u"List AI Jobs"_s, .category = u"AI"_s,
                                      .readOnly = true}));
    REQUIRE(registerTestCommand(bus, {.id = u"program.seek"_s, .title = u"Seek"_s, .category = u"Program"_s}));
    REQUIRE(registerTestCommand(bus, {.id = u"demo.run"_s, .title = u"Run Demo"_s, .category = u"Demo"_s}));

    mikserstudio::ui::CommandLogModel log(bus);
    QLabel label;
    mikserstudio::ui::CommandStatus status(bus, &label);
    status.showMessage(u"Ready"_s, false);

    REQUIRE(bus.execute(u"ai.jobs"_s));
    // Failures of read-only commands (e.g. polls while the service is down) are quiet too.
    CHECK_FALSE(bus.execute(u"ai.jobs"_s, QJsonObject{{u"unexpected"_s, 1}}));
    {
        const mikserstudio::ui::QuietCommandScope quiet;
        REQUIRE(bus.execute(u"program.seek"_s));
    }
    CHECK(log.rowCount() == 0);
    CHECK(status.text() == u"Ready"_s);

    REQUIRE(bus.execute(u"demo.run"_s));
    CHECK(log.rowCount() == 1);
    CHECK(status.text().startsWith(u"Run Demo"_s));
}
