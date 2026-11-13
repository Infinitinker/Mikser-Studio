// The backend contract: what an AI backend offers, how it says whether it can run, and the tasks it runs.
#pragma once

#include "mikserstudio/services/AiTypes.h"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include <expected>
#include <functional>
#include <memory>

namespace mikserstudio::services {

struct AiAvailability {
    bool available = false;
    QString reason;  // Why not, and what to do about it; empty when available.
};

// What a task made: files in its output folder, its part of the provenance and the operation's own
// result fields.
struct AiOutcome {
    QList<AiOutput> outputs;
    QJsonObject provenance;  // "tools" with their versions, "model", "method".
    QJsonObject data;  // The operation's own result fields, such as "text" or "seed".
};

struct AiTaskContext {
    QString jobId;
    QString outputDirectory;  // Exists; the task writes only here.
    // Progress from 0 to 1, and a short status for the interface.
    std::function<void(double progress, const QString& message)> report;
    // Runs exactly once, never from within AiBackend::start() and never after cancel(); the error
    // is an English message for the user.
    std::function<void(std::expected<AiOutcome, QString>)> finish;
};

// One run of an operation. Main thread only.
class AiTask : public QObject {
public:
    using QObject::QObject;

    // Stops the task and aborts its requests; finish is not called afterwards. A task never stops
    // work in the tool that is not its own.
    virtual void cancel() = 0;
};

// One AI tool's API: the operations it offers, whether it can run them now, and their tasks. A slow
// or hung tool never blocks: every request has a timeout. Main thread only.
class AiBackend : public QObject {
public:
    using QObject::QObject;

    [[nodiscard]] virtual QString id() const = 0;
    [[nodiscard]] virtual QString title() const = 0;
    [[nodiscard]] virtual QList<AiOperation> operations() const = 0;

    // The tool's address. New tasks and checks use it at once; running tasks go on with the one
    // they started with.
    virtual void setBaseUrl(const QUrl& url) = 0;

    // A cheap check, a few short requests; `done` runs once, later, while `context` is alive.
    virtual void checkAvailability(const QObject* context, std::function<void(AiAvailability)> done) = 0;

    // Starts `op` with params that passed its schema, with the defaults applied.
    [[nodiscard]] virtual std::unique_ptr<AiTask> start(const QString& op, const QJsonObject& params,
                                                        AiTaskContext context) = 0;

    // Whether requests are still on their way, such as one taking a canceled task's work off the
    // tool's queue. They go with the backend.
    [[nodiscard]] virtual bool hasPendingRequests() const { return false; }
};

}  // namespace mikserstudio::services
