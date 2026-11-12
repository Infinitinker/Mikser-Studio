// Interactive command runs for the UI: ask for the command's confirmation first, then run it on the command
// bus.
#pragma once

#include "mikserstudio/core/Command.h"

#include <QJsonObject>
#include <QString>

#include <expected>
#include <optional>

class QWidget;

namespace mikserstudio::core {
class CommandBus;
}

namespace mikserstudio::ui {

// Every interactive run of a command (menus, shortcuts, the palette, panels) goes through these
// functions, so a command with a CommandSpec::confirmation always asks the user first.

// True when the command may run: it has no confirmation text, or the user chose "Continue" in a
// question box over `parent` (the active window when null). Unknown commands return true; running
// them reports the error.
[[nodiscard]] bool confirmCommand(const core::CommandBus& bus, const QString& id, QWidget* parent);

// Asks for confirmation (see confirmCommand) and runs the command. std::nullopt when the user
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
// commands with required params get a dialog with a form generated from their schema. Returns
// std::nullopt when the user canceled (or the command is gone), otherwise the command's result.
std::optional<std::expected<QJsonObject, core::CommandError>> invokeCommand(core::CommandBus& bus, const QString& id,
                                                                            QWidget* parent);

}  // namespace mikserstudio::ui
