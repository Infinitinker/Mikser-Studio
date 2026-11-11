#pragma once

#include <QJsonObject>
#include <QMetaType>
#include <QString>

#include <expected>
#include <functional>
#include <memory>

namespace mikserstudio::core {

// Describes one action of the app. Every user-visible action is a command: menus, shortcuts,
// the command palette, macros, the /v1 API and plugins all go through the same registry.
struct CommandSpec {
    QString id;                // Stable, dotted, lower_snake: "timeline.add_clip".
    QString title;             // Short English UI label: "Add Clip to Timeline".
    QString description;       // One sentence for tooltips, the palette and API docs.
    QString category;          // Menu/palette group: "Timeline", "Edit", "AI".
    QJsonObject paramsSchema;  // JSON Schema (draft 2020-12 subset) of the params object.
    QString shortcut;          // Default key sequence in QKeySequence portable text, may be empty.
    bool undoable = false;     // True when a successful run pushes an UndoableEdit.
    // True when the command never changes state (a read such as "project.list"). UIs may keep it
    // out of logs and status messages; API clients may cache its result. Never also undoable.
    bool readOnly = false;
    // Non-empty: interactive UIs must ask the user with this English text (a full sentence, e.g.
    // "Start a new project? Unsaved changes are lost.") before running it. The API does not ask.
    QString confirmation = {};
};

// A change that can be reverted. Created by a command handler after it has applied the change.
class UndoableEdit {
public:
    virtual ~UndoableEdit() = default;
    virtual void undo() = 0;
    virtual void redo() = 0;
    // English text shown after "Undo"/"Redo" in the UI.
    [[nodiscard]] virtual QString text() const = 0;
};

enum class CommandErrorCode {
    UnknownCommand,
    InvalidParams,
    Failed,
    NotAvailable,  // Valid command that cannot run in the current state (e.g. nothing to undo).
};

// An error in user terms: `message` says what went wrong and what to do about it.
struct CommandError {
    CommandErrorCode code = CommandErrorCode::Failed;
    QString message;
};

struct CommandOutcome {
    QJsonObject result;                    // Returned to the caller (UI, API, macro).
    std::unique_ptr<UndoableEdit> edit;    // Set only by undoable commands that changed state.
};

using CommandResult = std::expected<CommandOutcome, CommandError>;

// Params have already been validated against CommandSpec::paramsSchema when this runs.
// Handlers run on the thread that owns the CommandBus (the main thread).
using CommandHandler = std::function<CommandResult(const QJsonObject& params)>;

[[nodiscard]] inline std::unexpected<CommandError> commandFailure(CommandErrorCode code, QString message)
{
    return std::unexpected(CommandError{code, std::move(message)});
}

}  // namespace mikserstudio::core

// Lets CommandBus::failed reach queued connections; CommandBus registers it.
Q_DECLARE_METATYPE(mikserstudio::core::CommandError)
