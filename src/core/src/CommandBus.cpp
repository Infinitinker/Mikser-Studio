// The command bus: registering, finding and running commands, and their undo steps.
#include "mikserstudio/core/CommandBus.h"

#include "CommandSpecCheck.h"
#include "Logging.h"
#include "mikserstudio/core/JsonSchema.h"
#include "mikserstudio/core/UndoStack.h"

#include <QJsonArray>

#include <QThread>

#include <exception>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

const QStringList& allPermissions()
{
    static const QStringList permissions{u"project.read"_s, u"project.write"_s, u"files"_s, u"network"_s,
                                         u"ui"_s,           u"plugins.manage"_s};
    return permissions;
}

CallContext CallContext::through(const QString& origin, const QStringList& permitted) const
{
    CallContext passed{.origins = origins, .permissions = {}, .confirmed = false};
    passed.origins.append(origin);
    for (const QString& permission : permissions) {
        if (permitted.contains(permission))
            passed.permissions.append(permission);
    }
    return passed;
}

namespace {

// Who `origin` is, for messages: "the interface", "the API client" or "the plugin “hello”".
QString describeOrigin(const QString& origin)
{
    if (origin == u"ui"_s)
        return u"the interface"_s;
    if (origin == u"api"_s)
        return u"the API client"_s;
    if (origin.startsWith(u"plugin:"_s))
        return u"the plugin “%1”"_s.arg(origin.mid(7));
    return u"“%1”"_s.arg(origin);
}

// Keeps a run's context on the bus's stack while its handler runs.
class RunningContext {
public:
    RunningContext(std::vector<CallContext>& running, const CallContext& context)
        : m_running(running)
    {
        m_running.push_back(context);
    }
    ~RunningContext() { m_running.pop_back(); }
    RunningContext(const RunningContext&) = delete;
    RunningContext& operator=(const RunningContext&) = delete;

private:
    std::vector<CallContext>& m_running;
};

CommandResult runHandler(const CommandHandler& handler, const QString& id, const QJsonObject& params)
{
    try {
        return handler(params);
    } catch (const std::exception& exception) {
        return commandFailure(
            CommandErrorCode::Failed,
            u"Command “%1” stopped with an internal error: %2. Report this problem, with Mikser Studio’s log."_s.arg(
                id, QString::fromLocal8Bit(exception.what())));
    }
}

QJsonObject undoRedoResultSchema()
{
    return objectResultSchema({{u"text"_s, u"string"_s}});
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
    qRegisterMetaType<CallContext>();
    registerCommand(
        CommandSpec{
            .id = u"edit.undo"_s,
            .title = u"Undo"_s,
            .description = u"Reverts the last change."_s,
            .category = u"Edit"_s,
            .paramsSchema = {},
            .shortcut = u"Ctrl+Z"_s,
            .undoable = false,
            .readOnly = false,
            .confirmation = {},
            .confirmationParam = {},
            .owner = u"core"_s,
            .resultSchema = undoRedoResultSchema(),
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
            .readOnly = false,
            .confirmation = {},
            .confirmationParam = {},
            .owner = u"core"_s,
            .resultSchema = undoRedoResultSchema(),
        },
        undoRedoHandler(m_undoStack, false));
}

CommandBus::~CommandBus() = default;

bool CommandBus::registerCommand(CommandSpec spec, CommandHandler handler)
{
    spec.async = false;
    if (!handler) {
        qCWarning(lcCommands).noquote() << "Rejected command" << spec.id << "because it has no handler";
        return false;
    }
    return add(std::move(spec), std::move(handler), {});
}

bool CommandBus::registerAsyncCommand(CommandSpec spec, AsyncCommandHandler handler)
{
    spec.async = true;
    if (!handler) {
        qCWarning(lcCommands).noquote() << "Rejected command" << spec.id << "because it has no handler";
        return false;
    }
    return add(std::move(spec), {}, std::move(handler));
}

bool CommandBus::add(CommandSpec spec, CommandHandler handler, AsyncCommandHandler asyncHandler)
{
    auto normalized = normalizeCommandSpec(std::move(spec));
    if (!normalized) {
        qCWarning(lcCommands).noquote() << "Rejected command:" << normalized.error();
        return false;
    }
    const QString id = normalized->id;
    if (m_commands.contains(id)) {
        qCWarning(lcCommands).noquote() << "Rejected command" << id << "because the id is already registered";
        return false;
    }
    const QString tool = toolNameOf(id);
    for (const auto& [taken, entry] : m_commands) {
        if (toolNameOf(taken) == tool) {
            qCWarning(lcCommands).noquote()
                << "Rejected command" << id << "because" << taken << "already has its MCP tool name" << tool;
            return false;
        }
    }
    m_commands.emplace(id, Entry{std::move(*normalized), std::move(handler), std::move(asyncHandler)});
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

void CommandBus::unregisterOwnedBy(const QString& owner)
{
    QStringList owned;
    for (const auto& [id, entry] : m_commands) {
        if (entry.spec.owner == owner)
            owned.append(id);
    }
    for (const QString& id : owned)
        unregisterCommand(id);
}

bool CommandBus::updateSpec(CommandSpec spec)
{
    const auto it = m_commands.find(spec.id);
    if (it == m_commands.end()) {
        qCWarning(lcCommands).noquote() << "Did not update" << spec.id << "because it isn't registered";
        return false;
    }
    if (spec.owner != it->second.spec.owner) {
        qCWarning(lcCommands).noquote() << "Did not update" << spec.id << "because it belongs to"
                                        << it->second.spec.owner << ", not" << spec.owner;
        return false;
    }
    spec.async = it->second.spec.async;  // The handler decides it.
    auto normalized = normalizeCommandSpec(std::move(spec));
    if (!normalized) {
        qCWarning(lcCommands).noquote() << "Did not update a command:" << normalized.error();
        return false;
    }
    it->second.spec = std::move(*normalized);
    qCDebug(lcCommands).noquote() << "Updated" << it->first;
    Q_EMIT commandChanged(it->first);
    return true;
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

const CallContext* CommandBus::runningContext() const
{
    return m_running.empty() ? nullptr : &m_running.back();
}

std::expected<QJsonObject, CommandError> CommandBus::execute(const QString& id, const QJsonObject& params)
{
    if (m_running.empty())
        return execute(id, params, CallContext::ui());
    CallContext inherited = m_running.back();
    inherited.confirmed = false;
    return execute(id, params, inherited);
}

std::expected<QJsonObject, CommandError> CommandBus::admit(const Entry& entry, const QJsonObject& params,
                                                           const CallContext& context) const
{
    const QString& id = entry.spec.id;
    if (m_running.size() >= kMaxDepth) {
        return std::unexpected(
            CommandError{CommandErrorCode::Failed,
                         u"“%1” was not run: commands ran other commands more than %2 levels deep, as in a "
                         u"loop. Report this problem, with Mikser Studio’s log."_s.arg(id)
                             .arg(kMaxDepth)});
    }
    if (const QString& permission = entry.spec.permission; !context.permissions.contains(permission)) {
        return std::unexpected(CommandError{
            CommandErrorCode::PermissionDenied,
            u"“%1” needs the “%2” permission, and %3 doesn’t have it. Grant it, or run the command yourself."_s.arg(
                id, permission, describeOrigin(context.origins.constLast()))});
    }
    QJsonObject given = params;
    if (context.origins.constLast().startsWith(u"plugin:"_s)) {
        // A plugin never agrees for the person: only their answer to the question
        // (ms.ui.confirm) does, and only for the call it was given for.
        if (!entry.spec.confirmation.isEmpty() && !context.confirmed)
            return std::unexpected(CommandError{CommandErrorCode::NeedsConfirmation, entry.spec.confirmation});
        if (const QString& agreement = entry.spec.confirmationParam; !agreement.isEmpty()) {
            given.remove(agreement);
            if (context.confirmed)
                given.insert(agreement, true);
        }
    }
    QJsonObject effective = JsonSchema::applyDefaults(given, entry.spec.paramsSchema);
    if (const QStringList problems = JsonSchema::validate(effective, entry.spec.paramsSchema); !problems.isEmpty()) {
        return std::unexpected(CommandError{CommandErrorCode::InvalidParams,
                                            u"Invalid parameters for “%1”: %2"_s.arg(id, problems.join(u"; "_s))});
    }
    return effective;
}

void CommandBus::report(const QString& id, const QJsonObject& params, const CommandError& error,
                        const CallContext& context)
{
    if (error.code == CommandErrorCode::NeedsConfirmation) {
        // A question for the caller to ask, not a failure to report.
        qCDebug(lcCommands).noquote() << id << "needs confirmation:" << error.message;
        return;
    }
    qCInfo(lcCommands).noquote() << id << "failed:" << error.message;
    Q_EMIT failed(id, params, error, context);
}

std::expected<QJsonObject, CommandError> CommandBus::execute(const QString& id, const QJsonObject& params,
                                                             const CallContext& context)
{
    const auto fail = [&](CommandError error) -> std::expected<QJsonObject, CommandError> {
        report(id, params, error, context);
        return std::unexpected(std::move(error));
    };

    const auto it = m_commands.find(id);
    if (it == m_commands.end()) {
        return fail(
            {CommandErrorCode::UnknownCommand,
             u"Unknown command “%1”. Open the command palette or call GET /v1/commands to see the available commands."_s
                 .arg(id)});
    }
    if (it->second.spec.async) {
        return fail({CommandErrorCode::NotAvailable,
                     u"“%1” answers later, and this caller can’t wait for it. Run it from a menu, the command palette "
                     u"or the API."_s.arg(id)});
    }
    auto effective = admit(it->second, params, context);
    if (!effective)
        return fail(std::move(effective.error()));
    // Copies, because a handler may unregister commands (e.g. a plugin deactivating itself).
    const bool undoable = it->second.spec.undoable;
    const CommandHandler handler = it->second.handler;

    CommandResult outcome = [&] {
        const RunningContext running(m_running, context);
        return runHandler(handler, id, *effective);
    }();
    if (!outcome)
        return fail(std::move(outcome.error()));

    if (outcome->edit) {
        if (!undoable)
            qCWarning(lcCommands).noquote() << id << "returned an undo edit but is not declared undoable";
        m_undoStack.push(std::move(outcome->edit));
    }
    qCDebug(lcCommands).noquote() << "Executed" << id;
    Q_EMIT executed(id, *effective, outcome->result, context);
    return std::move(outcome->result);
}

CommandBus::RunId CommandBus::executeAsync(const QString& id, const QJsonObject& params, const CallContext& context,
                                           QObject* receiver, ExecuteDone done)
{
    const RunId run = m_nextRun.fetch_add(1);
    Reply reply{receiver, std::move(done)};
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(
            this, [this, run, id, params, context, reply] { start(run, id, params, context, reply); },
            Qt::QueuedConnection);
        return run;
    }
    start(run, id, params, context, std::move(reply));
    return run;
}

void CommandBus::start(RunId run, const QString& id, const QJsonObject& params, const CallContext& context, Reply reply)
{
    const auto it = m_commands.find(id);
    if (it == m_commands.end() || !it->second.spec.async) {
        deliver(reply, execute(id, params, context));
        return;
    }
    auto effective = admit(it->second, params, context);
    if (!effective) {
        report(id, params, effective.error(), context);
        deliver(reply, std::unexpected(std::move(effective.error())));
        return;
    }
    // Copied, because the handler may unregister the command.
    const AsyncCommandHandler handler = it->second.asyncHandler;
    m_pending.emplace(run, PendingRun{id, *effective, context, it->second.spec.undoable, std::move(reply)});
    CommandDone done = [bus = QPointer<CommandBus>(this), run](CommandResult outcome) {
        if (!bus)
            return;
        if (QThread::currentThread() == bus->thread()) {
            bus->finish(run, std::move(outcome));
            return;
        }
        auto shared = std::make_shared<CommandResult>(std::move(outcome));
        QMetaObject::invokeMethod(
            bus.data(), [bus, run, shared] { bus->finish(run, std::move(*shared)); }, Qt::QueuedConnection);
    };
    const RunningContext running(m_running, context);
    try {
        handler(*effective, context, done);
    } catch (const std::exception& exception) {
        done(commandFailure(
            CommandErrorCode::Failed,
            u"Command “%1” stopped with an internal error: %2. Report this problem, with Mikser Studio’s log."_s.arg(
                id, QString::fromLocal8Bit(exception.what()))));
    }
}

void CommandBus::finish(RunId run, CommandResult outcome)
{
    auto node = m_pending.extract(run);
    if (node.empty())
        return;  // Canceled, or answered already.
    PendingRun& pending = node.mapped();
    if (!outcome) {
        report(pending.id, pending.params, outcome.error(), pending.context);
        deliver(pending.reply, std::unexpected(std::move(outcome.error())));
        return;
    }
    if (outcome->edit) {
        if (!pending.undoable)
            qCWarning(lcCommands).noquote() << pending.id << "returned an undo edit but is not declared undoable";
        m_undoStack.push(std::move(outcome->edit));
    }
    qCDebug(lcCommands).noquote() << "Executed" << pending.id;
    Q_EMIT executed(pending.id, pending.params, outcome->result, pending.context);
    deliver(pending.reply, std::move(outcome->result));
}

void CommandBus::cancel(RunId run, const QString& reason)
{
    auto node = m_pending.extract(run);
    if (node.empty())
        return;
    const PendingRun& pending = node.mapped();
    const CommandError error{CommandErrorCode::Canceled, reason};
    report(pending.id, pending.params, error, pending.context);
    deliver(pending.reply, std::unexpected(error));
}

void CommandBus::deliver(const Reply& reply, std::expected<QJsonObject, CommandError> result)
{
    QObject* receiver = reply.receiver.data();
    if (!receiver || !reply.done)
        return;
    if (receiver->thread() == QThread::currentThread()) {
        reply.done(std::move(result));
        return;
    }
    auto shared = std::make_shared<std::expected<QJsonObject, CommandError>>(std::move(result));
    QMetaObject::invokeMethod(
        receiver, [done = reply.done, shared] { done(std::move(*shared)); }, Qt::QueuedConnection);
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
        {u"confirmationParam"_s, spec.confirmationParam},
        {u"owner"_s, spec.owner},
        {u"resultSchema"_s, spec.resultSchema},
        {u"permission"_s, spec.permission},
        {u"startsJob"_s, spec.startsJob},
        {u"openWorld"_s, spec.openWorld},
        {u"quiet"_s, spec.quiet},
        {u"async"_s, spec.async},
    };
}

QJsonObject objectResultSchema(std::initializer_list<std::pair<QString, QString>> properties)
{
    QJsonObject schemas;
    QJsonArray required;
    for (const auto& [name, type] : properties) {
        schemas.insert(name, QJsonObject{{u"type"_s, type}});
        required.append(name);
    }
    return QJsonObject{{u"type"_s, u"object"_s}, {u"properties"_s, schemas}, {u"required"_s, required}};
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
    case CommandErrorCode::NeedsConfirmation:
        return u"needs_confirmation"_s;
    case CommandErrorCode::PermissionDenied:
        return u"permission_denied"_s;
    case CommandErrorCode::Canceled:
        return u"canceled"_s;
    }
    return u"failed"_s;
}

}  // namespace mikserstudio::core
