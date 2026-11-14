// One running Lua plugin, on the main thread: its worker thread and Lua state, the commands it
// registered, the runs, transactions and jobs it has open, its events and its hot reload.
#pragma once

#include "mikserstudio/core/Command.h"
#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/EventBus.h"
#include "mikserstudio/core/PluginManager.h"
#include "mikserstudio/core/PluginRuntime.h"
#include "mikserstudio/lua/LuaRuntime.h"

#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QThread>
#include <QTimer>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>

namespace mikserstudio::lua {

class LuaPlugin;
class LuaWorker;
struct WorkerShared;

// How a worker reaches its plugin on the main thread. A worker that is left running after its
// plugin has gone (it was stuck in a long C call) posts into nothing.
class MainLink {
public:
    explicit MainLink(LuaPlugin* plugin)
        : m_plugin(plugin)
    {
    }

    // Runs `call` on the plugin's thread, later; from any thread.
    void post(std::function<void(LuaPlugin&)> call);
    void detach();

private:
    std::mutex m_mutex;
    LuaPlugin* m_plugin;
};

class LuaPlugin final : public QObject, public core::PluginInstance {
    Q_OBJECT

public:
    LuaPlugin(LuaRuntimeOptions options, core::PluginManifest manifest, core::PluginActivation activation);
    ~LuaPlugin() override;
    LuaPlugin(const LuaPlugin&) = delete;
    LuaPlugin& operator=(const LuaPlugin&) = delete;

    // Starts the worker; the activation hears how it went.
    void start();
    void setSettings(const QJsonObject& settings) override;

    // What the worker of `generation` asks for. A request from a worker that has been replaced
    // is dropped.
    void registerCommand(quint64 generation, core::CommandSpec spec, qint64 handler);
    void run(quint64 generation, qint64 call, qint64 run, const QString& id, const QJsonObject& params);
    void done(quint64 generation, qint64 run, bool ok, const QJsonObject& value);
    void subscribe(quint64 generation, const QString& topic);
    void beginTransaction(quint64 generation, qint64 call, qint64 run, const QString& name);
    void finishTransaction(quint64 generation, qint64 call, qint64 transaction, bool commit, const QString& reason);
    void startJob(quint64 generation, qint64 call, qint64 run, const QString& kind, const QJsonObject& input);
    void started(quint64 generation, bool ok, const QString& message);

private:
    struct Run {
        core::CommandDone done;
        core::CallContext context;
    };

    [[nodiscard]] bool current(quint64 generation) const { return m_worker && generation == m_generation; }
    // The context of a call the plugin makes from the task of `run` (0: not in a command).
    [[nodiscard]] core::CallContext contextFor(qint64 run) const;
    void answer(qint64 call, const std::expected<QJsonValue, core::CommandError>& outcome);
    void startWorker();
    // Stops the worker and everything it had open: its runs finish as canceled, its commands are
    // unregistered, its transaction is rolled back, its jobs are canceled.
    void stopWorker(const QString& reason);
    void reload();
    void watchFiles();
    void deliver(const core::Event& event);

    LuaRuntimeOptions m_options;
    core::PluginManifest m_manifest;
    core::PluginActivation m_activation;
    QString m_origin;
    std::shared_ptr<MainLink> m_link;

    quint64 m_generation = 0;
    QThread* m_thread = nullptr;
    LuaWorker* m_worker = nullptr;
    std::shared_ptr<WorkerShared> m_shared;

    qint64 m_nextRun = 0;
    std::map<qint64, Run> m_runs;
    QSet<QString> m_topics;
    std::optional<core::CommandBus::TransactionId> m_transaction;
    std::set<core::CommandBus::TransactionId> m_rolledBack;
    QSet<QString> m_jobs;

    std::unique_ptr<QFileSystemWatcher> m_watcher;
    QTimer m_reloadTimer;
};

}  // namespace mikserstudio::lua
