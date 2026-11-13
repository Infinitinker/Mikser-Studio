// The AI engine: the backends that connect to the user's AI tools, their availability, and the jobs that run
// their operations.
#pragma once

#include "mikserstudio/core/Command.h"
#include "mikserstudio/services/AiSettings.h"
#include "mikserstudio/services/AiTypes.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace mikserstudio::services {

class AiBackend;
class AiTask;
struct AiOutcome;

struct AiEngineOptions {
    // Where jobs write their outputs, one folder per job. Required.
    QString cacheDirectory;
    // The AI settings file; empty keeps the settings in memory only.
    QString settingsFile;
};

// Runs AI operations in the user's AI tools, from C++ inside the app (docs/ai.md): one backend per
// tool API (ollama, comfyui). Jobs of one backend run one at a time, in order; jobs of different
// backends run side by side. Every request has a timeout, so a slow or hung tool never blocks the
// interface. Main thread only.
class AiEngine final : public QObject {
    Q_OBJECT

public:
    // Runs when a job's task has succeeded, with the job as jobsJson() lists it (its "result" set),
    // e.g. to bring the outputs into the project. Returns what it did (kept as the job's
    // "placement") or an error that fails the job.
    using FollowUp = std::function<std::expected<QJsonObject, QString>(const QJsonObject& job)>;

    // Finished jobs kept in jobsJson(); older ones are forgotten (their files stay in the cache).
    static constexpr int kMaxFinishedJobs = 200;

    explicit AiEngine(AiEngineOptions options, QObject* parent = nullptr);
    ~AiEngine() override;

    [[nodiscard]] const AiSettings& settings() const { return m_settings; }
    // Saves the settings and applies them at once: new jobs and checks use the new addresses, and
    // running jobs go on with the old ones.
    std::expected<void, QString> setSettings(const AiSettings& settings);

    // The backends with their operations and their last known availability. A backend not checked
    // yet is listed as unavailable, with a reason saying it is being checked.
    [[nodiscard]] QList<AiBackendInfo> backends() const;
    // Checks the backends again, in the background, unless a check is under way or recent.
    void refreshAvailability();
    // The ids of the operations the backends offer, each once, in backend order.
    [[nodiscard]] QStringList operationIds() const;

    // Queues `op` with `params` on `backendId`, or on the first backend that offers it (an
    // available one first) when `backendId` is empty. The params are checked against the
    // operation's schema, with its defaults applied. The job's title is the operation's, with
    // `subject` after it when there is one ("Generate image: a red fox"). Returns the job id
    // ("aijob-<uuid>").
    std::expected<QString, core::CommandError> submit(const QString& op, const QJsonObject& params,
                                                      const QString& backendId, const QString& subject,
                                                      FollowUp followUp = {});
    // Cancels a job that has not finished; returns the state it has now.
    std::expected<AiJobState, core::CommandError> cancel(const QString& jobId);

    // {id, op, backend, title, params, state, progress, message, error, createdAt, startedAt,
    //  finishedAt, result, placement}, oldest first.
    [[nodiscard]] QJsonArray jobsJson() const;
    [[nodiscard]] int activeJobCount() const;

Q_SIGNALS:
    void jobsChanged();
    void backendsChanged();

private:
    struct Job;
    struct Slot;

    void addBackend(std::unique_ptr<AiBackend> backend);
    void startNext(Slot& slot);
    void taskFinished(const QString& jobId, std::expected<AiOutcome, QString> outcome);
    void finish(Job& job, AiJobState state, const QString& message, const QString& error = {});
    void retireTask(Job& job);
    void forgetOldJobs();
    [[nodiscard]] Job* find(const QString& id) const;
    [[nodiscard]] Slot* slotFor(const QString& backendId) const;
    [[nodiscard]] QString jobFolder(const QString& jobId) const;
    [[nodiscard]] static QJsonObject toJson(const Job& job);

    AiEngineOptions m_options;
    AiSettings m_settings;
    std::vector<std::unique_ptr<Slot>> m_slots;
    std::vector<std::unique_ptr<Job>> m_jobs;
};

}  // namespace mikserstudio::services
