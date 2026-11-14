// Interactive command runs for the UI: ask for the command's confirmation first, then run it on the command
// bus.
#pragma once

#include "mikserstudio/core/Command.h"

#include <QJsonObject>
#include <QString>

#include <expected>
#include <functional>
#include <optional>

class QWidget;

namespace mikserstudio::core {
class CommandBus;
}

namespace mikserstudio::ui {

// Every interactive run of a command (menus, shortcuts, the palette, panels) goes through these
// functions, so a command with a CommandSpec::confirmation always asks the user first, and one with a
// CommandSpec::confirmationParam asks when a run needs it.

// What an interactive run ends with: std::nullopt when the user declined or canceled, otherwise the
// command's result.
using InteractiveResult = std::optional<std::expected<QJsonObject, core::CommandError>>;
using InteractiveReply = std::function<void(InteractiveResult result)>;

// True when the command may run: it has no confirmation text, or the user chose "Continue" in a
// question box over `parent` (the active window when null). Unknown commands return true; running
// them reports the error.
[[nodiscard]] bool confirmCommand(const core::CommandBus& bus, const QString& id, QWidget* parent);

// Runs the command. When this run needs the user's agreement (CommandSpec::confirmationParam), asks
// with the command's question and runs it again with the parameter set to true; a value for that
// parameter in `params` is ignored. std::nullopt when the user declined, otherwise the result.
std::optional<std::expected<QJsonObject, core::CommandError>> executeAsking(core::CommandBus& bus, const QString& id,
                                                                            const QJsonObject& params, QWidget* parent);
// executeAsking() for commands that may answer later (CommandSpec::async), through
// CommandBus::executeAsync: `reply` (which may be null) gets the outcome when the run has finished,
// for a command that isn't async before this returns. No reply comes when `parent` is destroyed
// first.
void executeAskingLater(core::CommandBus& bus, const QString& id, const QJsonObject& params, QWidget* parent,
                        const InteractiveReply& reply);
// confirmCommand(), then executeAskingLater().
void executeInteractiveLater(core::CommandBus& bus, const QString& id, const QJsonObject& params, QWidget* parent,
                             const InteractiveReply& reply);

// Asks for confirmation (see confirmCommand) and runs the command with executeAsking(). std::nullopt when the user
// canceled, otherwise the command's result. The bus also reports errors through failed(), which
// feeds the status bar and the log.
std::optional<std::expected<QJsonObject, core::CommandError>> executeInteractive(core::CommandBus& bus,
                                                                                 const QString& id,
                                                                                 const QJsonObject& params,
                                                                                 QWidget* parent);

// Like executeInteractive() for callers that do not need the result: the user sees the outcome in
// the status bar and the command log.
void executeReported(core::CommandBus& bus, const QString& id, const QJsonObject& params = {},
                     QWidget* parent = nullptr);

// True when the command cannot run without the user filling in params (its schema has
// required properties).
[[nodiscard]] bool needsParamsDialog(const core::CommandSpec& spec);

// Runs a command picked in a menu, by its shortcut or in the palette: asks for confirmation, then
// commands with required params get a dialog with a form generated from their schema. A command
// whose params are all optional runs at once, and gets the dialog only when it answers
// invalid_params (as "Save project" does before the project has a file). The command may answer
// later; `reply` (which may be null) gets std::nullopt when the user canceled (or the command is
// gone), otherwise the command's result.
void invokeCommand(core::CommandBus& bus, const QString& id, QWidget* parent, InteractiveReply reply = {});

}  // namespace mikserstudio::ui
