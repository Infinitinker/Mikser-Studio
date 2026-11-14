// The command bus: the one registry and dispatcher of commands, which menus, shortcuts, the command palette,
// plugins and the local API all run through.
#pragma once

#include "mikserstudio/core/Command.h"

#include <QJsonObject>
#include <QObject>

#include <initializer_list>
#include <map>
#include <utility>
#include <vector>

namespace mikserstudio::core {

class UndoStack;

// The single registry and dispatcher of commands.
//
// Built-in commands registered by the constructor: "edit.undo" (Ctrl+Z) and "edit.redo"
// (Ctrl+Shift+Z); they return {text} of the reverted/reapplied edit or fail with NotAvailable
// (also while an undo group is open, see UndoStack::beginGroup).
//
// Spec rules: the id matches ^[a-z0-9_]+(\.[a-z0-9_]+)+$ and, with "_" for ".", has at most 64
// characters, isn't "recent_events" and differs from every registered command's (the names of MCP
// tools); the title is not empty; paramsSchema has "type": "object" (an empty paramsSchema becomes
// one that accepts only {}); a readOnly command is neither undoable nor asks first; a
// confirmationParam names a boolean property with "default": false; a resultSchema, when given, is
// an object schema; the owner isn't empty; and the permission is a known one (an empty one becomes
// "project.read" for a read-only command and "project.write" for any other).
//
// Handlers may execute other commands. To make those one undo step, open a core::UndoGroup in the
// handler: the nested commands' edits become one compound edit, and an edit the handler itself
// returns is pushed separately after the group has closed. Commands that run each other more than
// kMaxDepth levels deep fail, as a loop would.
class CommandBus final : public QObject {
    Q_OBJECT

public:
    explicit CommandBus(UndoStack& undoStack, QObject* parent = nullptr);
    ~CommandBus() override;

    // Fails (returns false and logs) when the id is already taken or the spec is malformed.
    bool registerCommand(CommandSpec spec, CommandHandler handler);
    void unregisterCommand(const QString& id);

    [[nodiscard]] bool contains(const QString& id) const;
    [[nodiscard]] const CommandSpec* spec(const QString& id) const;
    [[nodiscard]] QList<CommandSpec> specs() const;  // Sorted by id.

    // Applies the schema defaults to params, validates the result against the spec's schema
    // (InvalidParams lists every problem), runs the handler with the defaulted params, pushes its
    // edit to the undo stack and emits executed() (with the defaulted params) or failed(); a run
    // that needs confirmation (NeedsConfirmation) emits neither.
    // A handler that throws is reported as Failed. The handler may unregister commands. A caller
    // without the spec's permission gets PermissionDenied, and the handler doesn't run.
    std::expected<QJsonObject, CommandError> execute(const QString& id, const QJsonObject& params,
                                                     const CallContext& context);
    // Runs with the context of the command that is running, so a handler's commands keep their
    // caller's origin and permissions (but not the person's agreement), and with CallContext::ui()
    // when none is running.
    std::expected<QJsonObject, CommandError> execute(const QString& id, const QJsonObject& params = {});

    // The context of the innermost command that is running, or nullptr.
    [[nodiscard]] const CallContext* runningContext() const;

    static constexpr std::size_t kMaxDepth = 16;

    [[nodiscard]] UndoStack& undoStack() const { return m_undoStack; }

Q_SIGNALS:
    void commandRegistered(const QString& id);
    void commandUnregistered(const QString& id);
    void executed(const QString& id, const QJsonObject& params, const QJsonObject& result,
                  const mikserstudio::core::CallContext& context);
    void failed(const QString& id, const QJsonObject& params, const mikserstudio::core::CommandError& error,
                const mikserstudio::core::CallContext& context);

private:
    struct Entry {
        CommandSpec spec;
        CommandHandler handler;
    };

    UndoStack& m_undoStack;
    std::map<QString, Entry> m_commands;
    std::vector<CallContext> m_running;  // The contexts of the commands running, innermost last.
};

// Converts a spec to the JSON published by the API and used by the command palette.
[[nodiscard]] QJsonObject toJson(const CommandSpec& spec);
// A CommandSpec::resultSchema for a result object that always has these properties, each named
// with its JSON type, such as {u"path"_s, u"string"_s}.
[[nodiscard]] QJsonObject objectResultSchema(std::initializer_list<std::pair<QString, QString>> properties);
// "unknown_command", "invalid_params", "failed", "not_available", "needs_confirmation",
// "permission_denied", "canceled".
[[nodiscard]] QString toString(CommandErrorCode code);

}  // namespace mikserstudio::core
