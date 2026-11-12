#include "mikserstudio/core/CommandBus.h"

#include "CommandSpecCheck.h"
#include "Logging.h"
#include "mikserstudio/core/JsonSchema.h"
#include "mikserstudio/core/UndoStack.h"

#include <exception>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

namespace {

CommandResult runHandler(const CommandHandler& handler, const QString& id, const QJsonObject& params)
{
    try {
        return handler(params);
    } catch (const std::exception& exception) {
        return commandFailure(CommandErrorCode::Failed,
                              u"Command \"%1\" stopped with an internal error: %2. Please report this problem."_s.arg(
                                  id, QString::fromLocal8Bit(exception.what())));
    }
}

CommandHandler undoRedoHandler(UndoStack& stack, bool undo)
{
    return [&stack, undo](const QJsonObject&) -> CommandResult {
        if (stack.isGrouping()) {
            return commandFailure(
                CommandErrorCode::NotAvailable,
                u"%1 is not available while a combined change is being recorded. Wait until the running command has finished."_s
                    .arg(undo ? u"Undo"_s : u"Redo"_s));
        }
        const QString text = undo ? stack.undoText() : stack.redoText();
        const bool done = undo ? stack.undo() : stack.redo();
        if (!done) {
            return commandFailure(CommandErrorCode::NotAvailable,
                                  undo ? u"There is nothing to undo."_s : u"There is nothing to redo."_s);
        }
        return CommandOutcome{QJsonObject{{u"text"_s, text}}, nullptr};
    };
}

}  // namespace

CommandBus::CommandBus(UndoStack& undoStack, QObject* parent)
    : QObject(parent)
    , m_undoStack(undoStack)
{
    qRegisterMetaType<CommandError>();
    registerCommand(
        CommandSpec{
            .id = u"edit.undo"_s,
            .title = u"Undo"_s,
            .description = u"Reverts the last change."_s,
            .category = u"Edit"_s,
            .paramsSchema = {},
            .shortcut = u"Ctrl+Z"_s,
            .undoable = false,
        },
        undoRedoHandler(m_undoStack, true));
    registerCommand(
        CommandSpec{
            .id = u"edit.redo"_s,
            .title = u"Redo"_s,
            .description = u"Applies the last undone change again."_s,
            .category = u"Edit"_s,
            .paramsSchema = {},
            .shortcut = u"Ctrl+Shift+Z"_s,
            .undoable = false,
        },
        undoRedoHandler(m_undoStack, false));
}

CommandBus::~CommandBus() = default;

bool CommandBus::registerCommand(CommandSpec spec, CommandHandler handler)
{
    auto normalized = normalizeCommandSpec(std::move(spec));
    if (!normalized) {
        qCWarning(lcCommands).noquote() << "Rejected command:" << normalized.error();
        return false;
    }
    if (!handler) {
        qCWarning(lcCommands).noquote() << "Rejected command" << normalized->id << "because it has no handler";
        return false;
    }
    const QString id = normalized->id;
    if (m_commands.contains(id)) {
        qCWarning(lcCommands).noquote() << "Rejected command" << id << "because the id is already registered";
        return false;
    }
    m_commands.emplace(id, Entry{std::move(*normalized), std::move(handler)});
    qCDebug(lcCommands).noquote() << "Registered" << id;
    Q_EMIT commandRegistered(id);
    return true;
}

void CommandBus::unregisterCommand(const QString& id)
{
    if (m_commands.erase(id) == 0)
        return;
    qCDebug(lcCommands).noquote() << "Unregistered" << id;
    Q_EMIT commandUnregistered(id);
}

bool CommandBus::contains(const QString& id) const
{
    return m_commands.contains(id);
}

const CommandSpec* CommandBus::spec(const QString& id) const
{
    const auto it = m_commands.find(id);
    return it == m_commands.end() ? nullptr : &it->second.spec;
}

QList<CommandSpec> CommandBus::specs() const
{
    QList<CommandSpec> result;
    result.reserve(static_cast<qsizetype>(m_commands.size()));
    for (const auto& [id, entry] : m_commands)
        result.append(entry.spec);
    return result;
}

std::expected<QJsonObject, CommandError> CommandBus::execute(const QString& id, const QJsonObject& params)
{
    const auto fail = [&](CommandError error) -> std::expected<QJsonObject, CommandError> {
        qCInfo(lcCommands).noquote() << id << "failed:" << error.message;
        Q_EMIT failed(id, params, error);
        return std::unexpected(std::move(error));
    };

    const auto it = m_commands.find(id);
    if (it == m_commands.end()) {
        return fail(
            {CommandErrorCode::UnknownCommand,
             u"Unknown command \"%1\". Open the command palette or call GET /v1/commands to see the available commands."_s
                 .arg(id)});
    }
    // Copies, because a handler may unregister commands (e.g. a plugin deactivating itself).
    const bool undoable = it->second.spec.undoable;
    const CommandHandler handler = it->second.handler;
    const QJsonObject effective = JsonSchema::applyDefaults(params, it->second.spec.paramsSchema);

    if (const QStringList problems = JsonSchema::validate(effective, it->second.spec.paramsSchema);
        !problems.isEmpty()) {
        return fail(
            {CommandErrorCode::InvalidParams, u"Invalid parameters for \"%1\": %2"_s.arg(id, problems.join(u"; "_s))});
    }

    CommandResult outcome = runHandler(handler, id, effective);
    if (!outcome)
        return fail(std::move(outcome.error()));

    if (outcome->edit) {
        if (!undoable)
            qCWarning(lcCommands).noquote() << id << "returned an undo edit but is not declared undoable";
        m_undoStack.push(std::move(outcome->edit));
    }
    qCDebug(lcCommands).noquote() << "Executed" << id;
    Q_EMIT executed(id, effective, outcome->result);
    return std::move(outcome->result);
}

QJsonObject toJson(const CommandSpec& spec)
{
    return QJsonObject{
        {u"id"_s, spec.id},
        {u"title"_s, spec.title},
        {u"description"_s, spec.description},
        {u"category"_s, spec.category},
        {u"paramsSchema"_s, spec.paramsSchema},
        {u"shortcut"_s, spec.shortcut},
        {u"undoable"_s, spec.undoable},
        {u"readOnly"_s, spec.readOnly},
        {u"confirmation"_s, spec.confirmation},
    };
}

QString toString(CommandErrorCode code)
{
    switch (code) {
    case CommandErrorCode::UnknownCommand:
        return u"unknown_command"_s;
    case CommandErrorCode::InvalidParams:
        return u"invalid_params"_s;
    case CommandErrorCode::Failed:
        return u"failed"_s;
    case CommandErrorCode::NotAvailable:
        return u"not_available"_s;
    }
    return u"failed"_s;
}

}  // namespace mikserstudio::core
