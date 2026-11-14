// Long work as jobs: lanes that run one job at a time, progress, cancel and results, the jobs.*
// commands and the job.* events.
#pragma once

#include "mikserstudio/core/Command.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>

#include <chrono>
#include <deque>
#include <expected>
#include <functional>
#include <map>

namespace mikserstudio::core {

class CommandBus;

enum class JobState { Queued, Running, Succeeded, Failed, Canceled };

// "queued", "running", "succeeded", "failed", "canceled".
[[nodiscard]] QString toString(JobState state);

struct JobSpec {
    QString id;  // Empty gives "job-<uuid>".
    QString kind;  // What the job does, such as "ai.image.generate".
    QString title;  // For the person: "Generate image: a red fox".
    QString owner = QStringLiteral("core");  // "core" or the plugin's id.
    // Jobs of one lane run one at a time, in the order they came; jobs of different lanes run side
    // by side. An empty lane runs the job at once.
    QString lane;
};

// Starts a job's work, on the main thread. The work reports through JobManager::progress and ends
// with succeed() or fail().
using JobRun = std::function<void(const QString& jobId)>;
// Stops a job's work, or forgets it when it hasn't started (a queued job). The job is canceled
// already; what the work reports afterwards is ignored.
using JobCancel = std::function<void(const QString& jobId)>;

// The app's jobs, on the main thread. Each change is published on the bus's events, with the
// origins of the command that started the job: "job.created" {jobId, kind, title, owner, lane},
// "job.progress" {jobId, progress, message} at most kProgressInterval apart per job, and
// "job.finished" {jobId, state, error, result}.
class JobManager final : public QObject {
    Q_OBJECT

public:
    static constexpr std::size_t kMaxFinishedJobs = 200;
    static constexpr std::chrono::milliseconds kProgressInterval{100};

    explicit JobManager(CommandBus& bus, QObject* parent = nullptr);
    ~JobManager() override;

    // Queues a job in its lane and returns its id; `run` starts it when the lane is free. Fails
    // when `spec.id` is taken.
    std::expected<QString, CommandError> submit(JobSpec spec, JobRun run, JobCancel cancel = {});
    // `progress` runs from 0 to 1. Ignored for a job that isn't running. succeed() and fail() are
    // ignored for a job that has finished.
    void progress(const QString& jobId, double progress, const QString& message = {});
    void succeed(const QString& jobId, const QJsonObject& result = {});
    void fail(const QString& jobId, const QString& error);
    // Cancels a job that hasn't finished and returns the state it has now.
    std::expected<JobState, CommandError> cancel(const QString& jobId);

    // {id, kind, title, owner, lane, state, progress, message, error, createdAt, startedAt,
    //  finishedAt, result, origins}, oldest first; the last kMaxFinishedJobs finished ones.
    [[nodiscard]] QJsonArray jobsJson() const;
    [[nodiscard]] std::optional<JobState> state(const QString& jobId) const;

Q_SIGNALS:
    void jobsChanged();

private:
    struct Job {
        JobSpec spec;
        JobRun run;
        JobCancel cancel;
        QStringList origins;
        JobState state = JobState::Queued;
        double progress = 0.0;
        QString message;
        QString error;
        QDateTime createdAt;
        QDateTime startedAt;
        QDateTime finishedAt;
        QJsonObject result;
        QElapsedTimer sinceProgress;  // Since the last job.progress; invalid before the first.
    };

    [[nodiscard]] Job* find(const QString& jobId);
    [[nodiscard]] const Job* find(const QString& jobId) const;
    void startNext(const QString& lane);
    void finish(Job& job, JobState state, const QString& error, const QJsonObject& result);
    void forgetOldJobs();
    void publish(const Job& job, const QString& topic, const QJsonObject& payload);
    [[nodiscard]] static QJsonObject toJson(const Job& job);

    CommandBus& m_bus;
    std::deque<Job> m_jobs;  // Oldest first.
    std::map<QString, QString> m_running;  // Lane -> the job running in it.
};

// Registers the job commands:
//   jobs.list    {}        -> {jobs: [...]} as JobManager::jobsJson() (readOnly)
//   jobs.cancel  {jobId}   -> {state}
// `jobs` and `bus` must outlive the registered commands.
void registerJobCommands(CommandBus& bus, JobManager& jobs);

}  // namespace mikserstudio::core
