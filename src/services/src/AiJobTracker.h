// The app's own list of AI jobs: submitting them to the AI service, polling them and failing them when the
// service stops.
#pragma once

#include "mikserstudio/core/Command.h"
#include "mikserstudio/services/AiTypes.h"
#include "mikserstudio/services/SidecarManager.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <expected>
#include <functional>
#include <optional>
#include <vector>

namespace mikserstudio::services {

class AiClient;

// Keeps the app's own list of AI jobs. Each job gets a local id at once, waits until the sidecar
// is running (starting it on demand), is submitted, and is polled while active. A sidecar crash
// fails the jobs it was running. Main thread only.
class AiJobTracker final : public QObject {
    Q_OBJECT

public:
    // Runs when the sidecar job has succeeded, e.g. to bring the result into the project. Returns
    // what it did (kept as the job's "placement") or an error that fails the job.
    using FollowUp = std::function<std::expected<QJsonObject, QString>(const AiJobInfo&)>;

    static constexpr int kPollIntervalMs = 500;

    AiJobTracker(SidecarManager& sidecar, AiClient& client, QObject* parent = nullptr);
    ~AiJobTracker() override;

    QString submit(const QString& op, const QJsonObject& params, const QString& title, FollowUp followUp = {});
    // Cancels a job that has not finished; returns the state it has now.
    std::expected<AiJobState, core::CommandError> cancel(const QString& jobId);

    [[nodiscard]] QJsonArray jobsJson() const;
    [[nodiscard]] int activeJobCount() const;

    // The backends the sidecar reported since it last became running; nullopt until it answered.
    [[nodiscard]] const std::optional<QList<AiBackendInfo>>& backends() const { return m_backends; }
    void refreshBackends();

    // Starts the sidecar when it is stopped or has failed.
    void ensureSidecar();
    [[nodiscard]] const SidecarManager* sidecar() const { return m_sidecar.data(); }

Q_SIGNALS:
    void jobsChanged();

private:
    struct Job {
        QString id;
        QString remoteId;
        QString op;
        QString title;
        QJsonObject params;
        AiJobState state = AiJobState::Queued;
        double progress = 0.0;
        QString message;
        QString error;
        QDateTime createdAt;
        QJsonObject result;
        QJsonObject placement;
        FollowUp followUp;
        bool submitting = false;
    };

    void onSidecarStateChanged(SidecarState state);
    void sendToSidecar(Job& job);
    void submitWaitingJobs();
    void poll();
    void apply(Job& job, const AiJobInfo& remote);
    void finish(Job& job, AiJobState state, const QString& message, const QString& error = {});
    [[nodiscard]] Job* find(const QString& id);
    [[nodiscard]] bool hasRemoteActiveJobs() const;
    [[nodiscard]] static QJsonObject toJson(const Job& job);

    QPointer<SidecarManager> m_sidecar;
    AiClient& m_client;
    std::vector<Job> m_jobs;
    std::optional<QList<AiBackendInfo>> m_backends;
    QTimer m_pollTimer;
    bool m_pollInFlight = false;
    bool m_backendsInFlight = false;
    // Bumped whenever the sidecar stops being running, so answers from an old process are ignored.
    quint64 m_generation = 0;
};

}  // namespace mikserstudio::services
