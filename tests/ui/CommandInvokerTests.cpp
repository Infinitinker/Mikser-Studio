// The command invoker: commands with a confirmation ask before an interactive run, a confirmation
// parameter is asked for only when a run needs it, a command that needs an optional param gets the
// params dialog, plugin panels get a canceled error when declined, and read-only commands stay out of
// the command log and status bar.
#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/PluginManager.h"
#include "mikserstudio/core/UndoStack.h"
#include "mikserstudio/ui/SchemaForm.h"

#include "CommandInvoker.h"
#include "CommandLogModel.h"
#include "CommandMenus.h"
#include "CommandStatus.h"
#include "MessageBoxAnswer.h"
#include "PluginPanelBridge.h"
#include "TestCommands.h"

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>

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
                                     .title = u"New project"_s,
                                     .category = u"File"_s,
                                     .shortcut = u"Ctrl+N"_s,
                                     .confirmation = kQuestion},
                                    &guardedRuns));
        REQUIRE(
            registerTestCommand(bus, {.id = u"demo.plain"_s, .title = u"Plain"_s, .category = u"Demo"_s}, &plainRuns));
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

TEST_CASE("A command asks for its confirmation parameter only when a run needs it", "[invoker]")
{
    mikserstudio::core::UndoStack undo;
    mikserstudio::core::CommandBus bus{undo};
    QWidget window;
    bool exists = false;
    QList<QJsonObject> runs;
    const auto schema = QJsonObject{
        {u"type"_s, u"object"_s},
        {u"properties"_s, QJsonObject{{u"overwrite"_s, QJsonObject{{u"type"_s, u"boolean"_s}, {u"default"_s, false}}}}},
        {u"additionalProperties"_s, false},
    };
    REQUIRE(bus.registerCommand({.id = u"demo.save"_s,
                                 .title = u"Save"_s,
                                 .description = u"Saves (test)."_s,
                                 .category = u"Demo"_s,
                                 .paramsSchema = schema,
                                 .shortcut = {},
                                 .confirmationParam = u"overwrite"_s},
                                [&](const QJsonObject& params) -> mikserstudio::core::CommandResult {
                                    runs.append(params);
                                    if (exists && !params.value(u"overwrite"_s).toBool())
                                        return mikserstudio::core::commandFailure(
                                            mikserstudio::core::CommandErrorCode::NeedsConfirmation, u"Replace it?"_s);
                                    return mikserstudio::core::CommandOutcome{};
                                }));

    // Nothing to replace: no question.
    REQUIRE(mikserstudio::ui::executeInteractive(bus, u"demo.save"_s, {}, &window).has_value());
    CHECK(runs.size() == 1);

    // The run needs agreement; declining runs nothing more, agreeing runs it again with the parameter.
    exists = true;
    QString shown;
    answerNextMessageBox(QMessageBox::RejectRole, &shown);
    CHECK_FALSE(mikserstudio::ui::executeInteractive(bus, u"demo.save"_s, {}, &window).has_value());
    CHECK(shown == u"Replace it?"_s);
    CHECK(runs.size() == 2);

    answerNextMessageBox(QMessageBox::AcceptRole);
    const auto agreed = mikserstudio::ui::executeInteractive(bus, u"demo.save"_s, {}, &window);
    REQUIRE(agreed.has_value());
    CHECK(agreed->has_value());
    REQUIRE(runs.size() == 4);
    CHECK(runs.last().value(u"overwrite"_s).toBool());

    // A caller cannot give the agreement itself.
    answerNextMessageBox(QMessageBox::RejectRole);
    CHECK_FALSE(mikserstudio::ui::executeInteractive(bus, u"demo.save"_s, QJsonObject{{u"overwrite"_s, true}}, &window)
                    .has_value());
    CHECK_FALSE(runs.last().value(u"overwrite"_s).toBool());
}

TEST_CASE("A command that needs an optional param gets the dialog, which asks before replacing", "[invoker][dialog]")
{
    mikserstudio::core::UndoStack undo;
    mikserstudio::core::CommandBus bus{undo};
    QWidget window;
    QList<QJsonObject> runs;
    const auto schema = QJsonObject{
        {u"type"_s, u"object"_s},
        {u"properties"_s, QJsonObject{{u"path"_s, QJsonObject{{u"type"_s, u"string"_s}}},
                                      {u"overwrite"_s, QJsonObject{{u"type"_s, u"boolean"_s}, {u"default"_s, false}}}}},
        {u"additionalProperties"_s, false},
    };
    // Like "Save project" before the project has a file: no path, no save.
    REQUIRE(bus.registerCommand({.id = u"demo.save"_s,
                                 .title = u"Save"_s,
                                 .description = u"Saves (test)."_s,
                                 .category = u"Demo"_s,
                                 .paramsSchema = schema,
                                 .shortcut = {},
                                 .confirmationParam = u"overwrite"_s},
                                [&](const QJsonObject& params) -> mikserstudio::core::CommandResult {
                                    runs.append(params);
                                    if (!params.contains(u"path"_s))
                                        return mikserstudio::core::commandFailure(
                                            mikserstudio::core::CommandErrorCode::InvalidParams, u"Give a path."_s);
                                    if (!params.value(u"overwrite"_s).toBool())
                                        return mikserstudio::core::commandFailure(
                                            mikserstudio::core::CommandErrorCode::NeedsConfirmation, u"Replace it?"_s);
                                    return mikserstudio::core::CommandOutcome{};
                                }));

    // When the dialog opens: fill in the path, arm the answer to the question, and run.
    QString shown;
    bool dialogShown = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        REQUIRE(dialog);
        dialogShown = true;
        auto* form = dialog->findChild<mikserstudio::ui::SchemaForm*>();
        REQUIRE(form);
        CHECK_FALSE(form->value().contains(u"overwrite"_s));  // Given by answering, not in the form.
        form->setValue({{u"path"_s, u"D:/cuts/first.msproj"_s}});
        answerNextMessageBox(QMessageBox::AcceptRole, &shown);
        const auto buttons = dialog->findChildren<QPushButton*>();
        const auto run = std::ranges::find_if(buttons, [](const QPushButton* b) { return b->text() == u"Run"_s; });
        REQUIRE(run != buttons.end());
        (*run)->click();
    });
    const auto saved = mikserstudio::ui::invokeCommand(bus, u"demo.save"_s, &window);
    CHECK(dialogShown);
    REQUIRE(saved.has_value());
    CHECK(saved->has_value());
    CHECK(shown == u"Replace it?"_s);
    REQUIRE(runs.size() == 3);
    CHECK(runs[1].value(u"path"_s) == u"D:/cuts/first.msproj"_s);
    CHECK(runs[2].value(u"overwrite"_s).toBool());
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

TEST_CASE("Plugin panels get a canceled error when the user declines the confirmation", "[invoker][plugin-panels]")
{
    BusFixture f;
    QTemporaryDir dir;
    mikserstudio::core::PluginManager plugins(f.bus, dir.filePath(u"plugins"_s));
    mikserstudio::ui::PluginPanelBridge bridge(f.bus, plugins, u"com.example.demo"_s);

    answerNextMessageBox(QMessageBox::RejectRole);
    const QVariantMap canceled = bridge.execute(u"project.new"_s);
    CHECK(canceled.value(u"ok"_s) == false);
    CHECK(canceled.value(u"error"_s).toMap().value(u"code"_s) == u"canceled"_s);
    CHECK(f.guardedRuns == 0);

    answerNextMessageBox(QMessageBox::AcceptRole);
    CHECK(bridge.execute(u"project.new"_s).value(u"ok"_s) == true);
    CHECK(f.guardedRuns == 1);
}

TEST_CASE("Read-only and quiet commands stay out of the command log and the status bar", "[invoker][log]")
{
    mikserstudio::core::UndoStack undo;
    mikserstudio::core::CommandBus bus(undo);
    REQUIRE(registerTestCommand(
        bus, {.id = u"ai.jobs"_s, .title = u"List AI jobs"_s, .category = u"AI"_s, .readOnly = true}));
    REQUIRE(registerTestCommand(
        bus, {.id = u"program.seek"_s, .title = u"Seek"_s, .category = u"Program"_s, .quiet = true}));
    REQUIRE(registerTestCommand(bus, {.id = u"demo.run"_s, .title = u"Run Demo"_s, .category = u"Demo"_s}));

    mikserstudio::ui::CommandLogModel log(bus);
    QLabel label;
    mikserstudio::ui::CommandStatus status(bus, &label);
    status.showMessage(u"Ready"_s, false);

    REQUIRE(bus.execute(u"ai.jobs"_s));
    // Failures of read-only commands (e.g. polls while the service is down) are quiet too.
    CHECK_FALSE(bus.execute(u"ai.jobs"_s, QJsonObject{{u"unexpected"_s, 1}}));
    REQUIRE(bus.execute(u"program.seek"_s));
    CHECK(log.rowCount() == 0);
    CHECK(status.text() == u"Ready"_s);

    REQUIRE(bus.execute(u"demo.run"_s));
    CHECK(log.rowCount() == 1);
    CHECK(status.text().startsWith(u"Run Demo"_s));
}
