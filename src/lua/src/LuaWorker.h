// The worker side of a Lua plugin: its Lua state, on a thread it never leaves.
#pragma once

#include "mikserstudio/core/EventBus.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>

namespace mikserstudio::core {
struct ProjectSnapshot;
}

namespace mikserstudio::lua {

class MainLink;

struct WorkerConfig {
    QString pluginId;
    QString pluginName;  // The category of its commands, unless they name one.
    QString directory;  // The plugin's folder; modules are read only from it.
    QString entry;  // The entry file, relative to the folder.
    quint64 generation = 0;
    std::size_t memoryLimit = 0;
    std::chrono::milliseconds runLimit{0};
    std::function<std::shared_ptr<const core::ProjectSnapshot>()> snapshot;
};

// What a worker and its plugin share across their threads.
struct WorkerShared {
    std::mutex mutex;
    QJsonObject settings;  // Guarded by `mutex`.
    core::EventInbox inbox;
    std::atomic<bool> drainQueued = false;  // A drainEvents() is on its way to the worker.
    std::atomic<bool> stopping = false;  // Plugin code stops at its next instructions.
};

class LuaWorker final : public QObject {
    Q_OBJECT

public:
    LuaWorker(WorkerConfig config, std::shared_ptr<WorkerShared> shared, std::shared_ptr<MainLink> link);
    ~LuaWorker() override;
    LuaWorker(const LuaWorker&) = delete;
    LuaWorker& operator=(const LuaWorker&) = delete;

    // On the worker's thread, each queued by the plugin.
    // Creates the sandboxed state, runs the prelude and then the plugin's entry file.
    void start();
    // Runs the handler registered as `handler` for the run `run`.
    void invoke(qint64 run, qint64 handler, const QJsonObject& params, const QJsonObject& context);
    // Runs the code of the plugin's own job `jobId` (ms.jobs.run) as the task `run`.
    void runJob(qint64 run, const QString& jobId);
    // Resumes the task waiting for `call` with its answer: the value, or {code, message}.
    void answer(qint64 call, bool ok, const QJsonValue& value);
    // Hands the events in the inbox to the plugin's callbacks.
    void drainEvents();
    // Closes the state, then ends the thread's event loop.
    void shutdown();

private:
    struct State;
    std::unique_ptr<State> m_state;
};

}  // namespace mikserstudio::lua
