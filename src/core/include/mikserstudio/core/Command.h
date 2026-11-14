// The command: its spec (id, title, parameter schema), its handler, its result and error types, and the
// undoable edit a command can return.
#pragma once

#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QStringList>

#include <expected>
#include <functional>
#include <memory>

namespace mikserstudio::core {

// Describes one action of the app. Every user-visible action is a command: menus, shortcuts,
// the command palette, macros, the /v1 API and plugins all go through the same registry.
struct CommandSpec {
    QString id;  // Stable, dotted, lower_snake: "timeline.add_clip".
    // Short English label in sentence case, as a menu item reads: "Add clip to timeline".
    QString title;
    // One sentence in the third person for tooltips, the palette and API docs: "Adds a clip to a track.".
    QString description;
    QString category;  // Menu/palette group: "Timeline", "Edit", "AI".
    QJsonObject paramsSchema;  // JSON Schema (draft 2020-12 subset) of the params object.
    QString shortcut;  // Default key sequence in QKeySequence portable text, may be empty.
    bool undoable = false;  // True when a successful run pushes an UndoableEdit.
    // True when the command never changes state (a read such as "project.list"). UIs may keep it
    // out of logs and status messages; API clients may cache its result. Never also undoable.
    bool readOnly = false;
    // Non-empty: interactive UIs must ask the user with this English text (a full sentence, e.g.
    // "Start a new project? Unsaved changes are lost.") before running it. The API does not ask.
    QString confirmation = {};
    // Set when only some runs need the user's agreement: the name of a boolean parameter, such as
    // "overwrite", that gives it. A run that needs it and lacks the parameter fails with
    // CommandErrorCode::NeedsConfirmation, whose message is the question to ask; the caller asks the
    // user and runs the command again with the parameter set to true. Interactive UIs and the MCP
    // bridge set it only after the user agreed.
    QString confirmationParam = {};
    // Who registered the command: "core" for the app's own commands, or the plugin's id.
    QString owner = QStringLiteral("core");
    // JSON Schema ("type": "object") of the result; empty when the result is not described.
    QJsonObject resultSchema = {};
    // The permission a caller needs to run it (docs/architecture.md, "Permissions"): "project.read",
    // "project.write", "files", "network", "ui" or "plugins.manage". Empty becomes "project.read" for
    // a read-only command and "project.write" for any other.
    QString permission = {};
    bool startsJob = false;  // The work goes on in a job; the result names it ({jobId}).
    bool openWorld = false;  // Reaches outside the app, such as an AI tool's API.
    // Logs and status messages leave it out, as they leave out read-only commands: bookkeeping such
    // as the many seeks of scrubbing the playhead.
    bool quiet = false;
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
    NeedsConfirmation,  // This run needs the user's agreement first (see CommandSpec::confirmationParam).
    PermissionDenied,  // The caller lacks the command's permission (CommandSpec::permission).
    Canceled,  // The run was stopped before it finished, by the caller or a timeout.
};

// Every permission, in the order docs/architecture.md ("Permissions") lists them.
[[nodiscard]] const QStringList& allPermissions();

// Who runs a command: the call context of docs/architecture.md ("Terms").
struct CallContext {
    // The chain of origins, the one that started the run first: "ui" (the person, in the
    // interface), "api" (a client of the local API, such as an agent) or "plugin:<id>".
    QStringList origins{QStringLiteral("ui")};
    // The permissions the caller carries. The person and the API carry them all.
    QStringList permissions = allPermissions();
    // The person agreed to this run, as the command's confirmation asks.
    bool confirmed = false;

    [[nodiscard]] static CallContext ui() { return {}; }
    [[nodiscard]] static CallContext api() { return {.origins = {QStringLiteral("api")}}; }
    // Who started the run.
    [[nodiscard]] const QString& origin() const { return origins.constFirst(); }
    // This context passed on by `origin`, such as a plugin, which carries at most `permitted`. The
    // person's agreement covers only the run it was given for.
    [[nodiscard]] CallContext through(const QString& origin, const QStringList& permitted) const;
};

// An error in user terms: `message` says what went wrong and what to do about it.
struct CommandError {
    CommandErrorCode code = CommandErrorCode::Failed;
    QString message;
};

struct CommandOutcome {
    QJsonObject result;  // Returned to the caller (UI, API, macro).
    std::unique_ptr<UndoableEdit> edit;  // Set only by undoable commands that changed state.
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

// Let CommandBus::executed and failed reach queued connections; CommandBus registers them.
Q_DECLARE_METATYPE(mikserstudio::core::CommandError)
Q_DECLARE_METATYPE(mikserstudio::core::CallContext)
