// A Lua plugin on the main thread: starting and stopping its worker, answering what its code asks
// for through the command bus and the job manager, handing it its events, and reloading it when
// its files change.
#include "LuaPlugin.h"

#include "Logging.h"
#include "LuaWorker.h"
#include "mikserstudio/core/JobManager.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QDirIterator>
#include <QJsonArray>

#include <tuple>
#include <utility>

using namespace Qt::StringLiterals;

namespace mikserstudio::lua {

namespace {

// Editors save in bursts (write, rename, write again): one reload after the last change.
constexpr std::chrono::milliseconds kReloadDelay{300};

// The codes a plugin's handler may answer with; any other becomes "failed".
core::CommandErrorCode errorCode(const QString& code)
{
    if (code == u"invalid_params")
        return core::CommandErrorCode::InvalidParams;
    if (code == u"not_available")
        return core::CommandErrorCode::NotAvailable;
    if (code == u"permission_denied")
        return core::CommandErrorCode::PermissionDenied;
    if (code == u"canceled")
        return core::CommandErrorCode::Canceled;
    return core::CommandErrorCode::Failed;
}

QJsonObject errorJson(const core::CommandError& error)
{
    return {{u"code"_s, core::toString(error.code)}, {u"message"_s, error.message}};
}

bool matches(const QSet<QString>& topics, const QString& topic)
{
    if (topics.contains(topic))
        return true;
    const qsizetype dot = topic.lastIndexOf(u'.');
    return dot > 0 && topics.contains(topic.left(dot) + u".*"_s);
}

}  // namespace

void MainLink::post(std::function<void(LuaPlugin&)> call)
{
    const std::scoped_lock lock(m_mutex);
    if (!m_plugin)
        return;
    LuaPlugin* plugin = m_plugin;
    QMetaObject::invokeMethod(plugin, [plugin, call = std::move(call)] { call(*plugin); }, Qt::QueuedConnection);
}

void MainLink::detach()
{
    const std::scoped_lock lock(m_mutex);
    m_plugin = nullptr;
}

LuaPlugin::LuaPlugin(LuaRuntimeOptions options, core::PluginManifest manifest, core::PluginActivation activation)
    : m_options(std::move(options))
    , m_manifest(std::move(manifest))
    , m_activation(std::move(activation))
    , m_origin(u"plugin:"_s + m_manifest.id)
    , m_link(std::make_shared<MainLink>(this))
{
    connect(&m_options.bus->events(), &core::EventBus::published, this, &LuaPlugin::deliver);
    connect(m_options.bus, &core::CommandBus::transactionRolledBack, this,
            [this](core::CommandBus::TransactionId transaction, const QString& reason) {
                if (m_transaction != transaction)
                    return;
                qCWarning(lcLua).noquote() << u"[%1]"_s.arg(m_manifest.id) << "transaction rolled back:" << reason;
                m_transaction.reset();
                m_rolledBack.insert(transaction);
            });
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(kReloadDelay);
    connect(&m_reloadTimer, &QTimer::timeout, this, &LuaPlugin::reload);
}

LuaPlugin::~LuaPlugin()
{
    stopWorker(u"The plugin “%1” stopped."_s.arg(m_manifest.name));
    m_link->detach();
}

void LuaPlugin::start()
{
    if (m_options.hotReload)
        watchFiles();
    startWorker();
}

void LuaPlugin::setSettings(const QJsonObject& settings)
{
    m_activation.settings = settings;
    if (m_shared) {
        const std::scoped_lock lock(m_shared->mutex);
        m_shared->settings = settings;
    }
}

void LuaPlugin::startWorker()
{
    ++m_generation;
    m_shared = std::make_shared<WorkerShared>();
    m_shared->settings = m_activation.settings;
    WorkerConfig config{
        .pluginId = m_manifest.id,
        .pluginName = m_manifest.name,
        .directory = m_manifest.directory,
        .entry = m_manifest.entry,
        .generation = m_generation,
        .memoryLimit = m_options.memoryLimit,
        .runLimit = m_options.runLimit,
        .snapshot = m_options.snapshot,
    };
    m_thread = new QThread;  // Deleted in stopWorker(), unless it never stops.
    m_thread->setObjectName(u"lua:%1"_s.arg(m_manifest.id));
    m_worker = new LuaWorker(std::move(config), m_shared, m_link);
    m_worker->moveToThread(m_thread);
    m_thread->start();
    LuaWorker* worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker] { worker->start(); }, Qt::QueuedConnection);
}

void LuaPlugin::stopWorker(const QString& reason)
{
    if (!m_worker)
        return;
    ++m_generation;  // What the old worker still sends is dropped.
    m_shared->stopping = true;

    // Everything the plugin had open ends now, as if it had answered.
    std::map<qint64, Run> runs = std::exchange(m_runs, {});
    for (auto& [run, pending] : runs)
        pending.done(core::commandFailure(core::CommandErrorCode::Canceled, reason));
    m_options.bus->unregisterOwnedBy(m_manifest.id);
    if (m_transaction) {
        m_options.bus->rollBackTransaction(*m_transaction, reason);
        m_transaction.reset();
    }
    for (const QString& job : std::as_const(m_jobs))
        std::ignore = m_options.jobs->cancel(job);
    m_jobs.clear();
    m_topics.clear();

    LuaWorker* worker = std::exchange(m_worker, nullptr);
    QThread* thread = std::exchange(m_thread, nullptr);
    m_shared.reset();
    QMetaObject::invokeMethod(worker, [worker] { worker->shutdown(); }, Qt::QueuedConnection);
    if (thread->wait(QDeadlineTimer(m_options.stopTimeout))) {
        delete worker;
        delete thread;
        return;
    }
    // It can't be stopped safely: it stays, and what it sends goes nowhere once this plugin has
    // gone.
    qCWarning(lcLua).noquote() << u"[%1]"_s.arg(m_manifest.id) << "did not stop within" << m_options.stopTimeout.count()
                               << "ms; its thread is left running";
}

void LuaPlugin::reload()
{
    qCInfo(lcLua).noquote() << u"[%1]"_s.arg(m_manifest.id) << "reloading after a change to its files";
    stopWorker(u"The plugin “%1” is reloading."_s.arg(m_manifest.name));
    watchFiles();
    startWorker();
}

void LuaPlugin::watchFiles()
{
    // Saving by writing a new file and renaming it over the old one drops the watch on the old
    // file, so every reload watches the files anew.
    m_watcher = std::make_unique<QFileSystemWatcher>();
    QStringList paths{m_manifest.directory};
    QDirIterator files(m_manifest.directory, {u"*.lua"_s, u"*.json"_s}, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext())
        paths.append(files.next());
    m_watcher->addPaths(paths);
    connect(m_watcher.get(), &QFileSystemWatcher::fileChanged, &m_reloadTimer, qOverload<>(&QTimer::start));
    connect(m_watcher.get(), &QFileSystemWatcher::directoryChanged, &m_reloadTimer, qOverload<>(&QTimer::start));
}

core::CallContext LuaPlugin::contextFor(qint64 run) const
{
    if (const auto it = m_runs.find(run); it != m_runs.end())
        return it->second.context.through(m_origin, m_activation.granted);
    return core::CallContext{.origins = {m_origin}, .permissions = m_activation.granted, .confirmed = false};
}

void LuaPlugin::answer(qint64 call, const std::expected<QJsonValue, core::CommandError>& outcome)
{
    if (!m_worker)
        return;
    LuaWorker* worker = m_worker;
    const bool ok = outcome.has_value();
    const QJsonValue value = ok ? *outcome : QJsonValue(errorJson(outcome.error()));
    QMetaObject::invokeMethod(
        worker, [worker, call, ok, value] { worker->answer(call, ok, value); }, Qt::QueuedConnection);
}

void LuaPlugin::registerCommand(quint64 generation, core::CommandSpec spec, qint64 handler)
{
    if (!current(generation))
        return;
    const QString id = spec.id;
    const bool added = m_options.bus->registerAsyncCommand(
        std::move(spec), [this, generation, handler](const QJsonObject& params, const core::CallContext& context,
                                                     core::CommandDone done) {
            if (!current(generation)) {
                done(core::commandFailure(core::CommandErrorCode::NotAvailable,
                                          u"The plugin “%1” is restarting; try again."_s.arg(m_manifest.name)));
                return;
            }
            const qint64 run = ++m_nextRun;
            m_runs.emplace(run, Run{std::move(done), context});
            const QJsonObject callerContext{{u"origin"_s, context.origin()},
                                            {u"origins"_s, QJsonArray::fromStringList(context.origins)}};
            LuaWorker* worker = m_worker;
            QMetaObject::invokeMethod(
                worker,
                [worker, run, handler, params, callerContext] { worker->invoke(run, handler, params, callerContext); },
                Qt::QueuedConnection);
        });
    if (!added) {
        qCWarning(lcLua).noquote() << u"[%1]"_s.arg(m_manifest.id) << "could not register" << id
                                   << (m_options.bus->contains(id) ? "(the id is taken)" : "(see the log above)");
    }
}

void LuaPlugin::run(quint64 generation, qint64 call, qint64 run, const QString& id, const QJsonObject& params)
{
    if (!current(generation))
        return;
    m_options.bus->executeAsync(id, params, contextFor(run), this,
                                [this, generation, call](std::expected<QJsonObject, core::CommandError> result) {
                                    if (!current(generation))
                                        return;
                                    if (result)
                                        answer(call, QJsonValue(*result));
                                    else
                                        answer(call, std::unexpected(result.error()));
                                });
}

void LuaPlugin::done(quint64 generation, qint64 run, bool ok, const QJsonObject& value)
{
    if (!current(generation))
        return;
    const auto it = m_runs.find(run);
    if (it == m_runs.end())
        return;
    core::CommandDone finish = std::move(it->second.done);
    m_runs.erase(it);
    if (ok) {
        finish(core::CommandOutcome{value, nullptr});
        return;
    }
    finish(core::commandFailure(errorCode(value.value(u"code").toString()), value.value(u"message").toString()));
}

void LuaPlugin::subscribe(quint64 generation, const QString& topic)
{
    if (current(generation))
        m_topics.insert(topic);
}

void LuaPlugin::deliver(const core::Event& event)
{
    if (event.topic == u"job.finished")
        m_jobs.remove(event.payload.value(u"jobId").toString());
    if (!m_worker || !matches(m_topics, event.topic))
        return;
    m_shared->inbox.push(event);
    if (!m_shared->drainQueued.exchange(true)) {
        LuaWorker* worker = m_worker;
        QMetaObject::invokeMethod(worker, [worker] { worker->drainEvents(); }, Qt::QueuedConnection);
    }
}

void LuaPlugin::beginTransaction(quint64 generation, qint64 call, qint64 run, const QString& name)
{
    if (!current(generation))
        return;
    auto transaction = m_options.bus->beginTransaction(name, contextFor(run));
    if (!transaction) {
        answer(call, std::unexpected(transaction.error()));
        return;
    }
    m_transaction = *transaction;
    answer(call, QJsonValue(static_cast<qint64>(*transaction)));
}

void LuaPlugin::finishTransaction(quint64 generation, qint64 call, qint64 transaction, bool commit,
                                  const QString& reason)
{
    if (!current(generation))
        return;
    const auto id = static_cast<core::CommandBus::TransactionId>(transaction);
    if (m_rolledBack.erase(id) > 0) {
        answer(call,
               std::unexpected(core::CommandError{
                   core::CommandErrorCode::Failed,
                   u"The transaction took longer than %1 s and was rolled back; none of its changes were kept."_s.arg(
                       core::CommandBus::kTransactionDeadline.count() / 1000.0)}));
        return;
    }
    if (m_transaction != id) {
        answer(call, std::unexpected(core::CommandError{core::CommandErrorCode::NotAvailable,
                                                        u"The transaction is no longer open."_s}));
        return;
    }
    m_transaction.reset();
    if (commit)
        m_options.bus->commitTransaction(id);
    else
        m_options.bus->rollBackTransaction(id, reason);
    answer(call, QJsonValue(true));
}

void LuaPlugin::startJob(quint64 generation, qint64 call, qint64 run, const QString& kind, const QJsonObject& input)
{
    if (!current(generation))
        return;
    auto job = m_options.jobs->start(kind, input, contextFor(run));
    if (!job) {
        answer(call, std::unexpected(job.error()));
        return;
    }
    if (m_options.jobs->state(*job).value_or(core::JobState::Succeeded) <= core::JobState::Running)
        m_jobs.insert(*job);
    answer(call, QJsonValue(*job));
}

void LuaPlugin::started(quint64 generation, bool ok, const QString& message)
{
    if (!current(generation))
        return;
    if (ok) {
        qCInfo(lcLua).noquote() << u"[%1]"_s.arg(m_manifest.id) << "started";
        if (m_activation.started)
            m_activation.started();
        return;
    }
    const QString failure = u"The plugin “%1” could not start: %2"_s.arg(m_manifest.name, message);
    stopWorker(failure);
    if (m_activation.failed)
        m_activation.failed(failure);
}

}  // namespace mikserstudio::lua
