// Submitting, polling, canceling and finishing AI jobs, and starting the AI service when a job needs it.
#include "AiJobTracker.h"

#include "Logging.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/services/AiClient.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mikserstudio::services {

namespace {

const QString kWaitingForService = u"Waiting for the AI service to start"_s;
const QString kLostWithService = u"The AI service stopped while the job was running. Start the job again."_s;

}  // namespace

AiJobTracker::AiJobTracker(SidecarManager& sidecar, AiClient& client, QObject* parent)
    : QObject(parent)
    , m_sidecar(&sidecar)
    , m_client(client)
{
    m_pollTimer.setInterval(kPollIntervalMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &AiJobTracker::poll);
    connect(&sidecar, &SidecarManager::stateChanged, this, &AiJobTracker::onSidecarStateChanged);
    if (sidecar.state() == SidecarState::Running)
        onSidecarStateChanged(SidecarState::Running);
}

AiJobTracker::~AiJobTracker() = default;

QString AiJobTracker::submit(const QString& op, const QJsonObject& params, const QString& title, FollowUp followUp)
{
    m_jobs.push_back(Job{
        .id = core::ProjectModel::newId(u"aijob"_s),
        .remoteId = {},
        .op = op,
        .title = title,
        .params = params,
        .state = AiJobState::Queued,
        .progress = 0.0,
        .message = kWaitingForService,
        .error = {},
        .createdAt = QDateTime::currentDateTimeUtc(),
        .result = {},
        .placement = {},
        .followUp = std::move(followUp),
        .submitting = false,
    });
    Job& job = m_jobs.back();
    const QString id = job.id;
    qCInfo(lcAi).noquote() << "New AI job" << id << op << "-" << title;

    if (!m_sidecar)
        finish(job, AiJobState::Failed, u"Failed"_s, u"The AI service is not available in this session."_s);
    else if (m_sidecar->state() == SidecarState::Running)
        sendToSidecar(job);
    else
        ensureSidecar();
    Q_EMIT jobsChanged();
    return id;
}

std::expected<AiJobState, core::CommandError> AiJobTracker::cancel(const QString& jobId)
{
    Job* job = find(jobId);
    if (!job) {
        return core::commandFailure(core::CommandErrorCode::Failed,
                                    u"There is no AI job %1. Run ai.jobs to see the jobs."_s.arg(jobId));
    }
    if (isFinished(job->state)) {
        return core::commandFailure(core::CommandErrorCode::NotAvailable,
                                    u"The AI job “%1” has already finished."_s.arg(job->title));
    }
    if (job->remoteId.isEmpty()) {
        // Not in the sidecar yet; a submission still on its way is canceled when it arrives.
        finish(*job, AiJobState::Canceled, u"Canceled"_s);
        Q_EMIT jobsChanged();
        return job->state;
    }

    job->message = u"Canceling"_s;
    m_client.cancelJob(job->remoteId, this,
                       [this, jobId, generation = m_generation](std::expected<AiJobInfo, QString> answer) {
                           Job* canceled = find(jobId);
                           if (!canceled || generation != m_generation || isFinished(canceled->state))
                               return;
                           if (answer)
                               apply(*canceled, *answer);
                           else
                               qCWarning(lcAi).noquote() << "Could not cancel" << jobId << "-" << answer.error();
                           Q_EMIT jobsChanged();
                       });
    Q_EMIT jobsChanged();
    return job->state;
}

QJsonArray AiJobTracker::jobsJson() const
{
    QJsonArray jobs;
    for (const Job& job : m_jobs)
        jobs.append(toJson(job));
    return jobs;
}

int AiJobTracker::activeJobCount() const
{
    return static_cast<int>(std::ranges::count_if(m_jobs, [](const Job& job) { return !isFinished(job.state); }));
}

void AiJobTracker::refreshBackends()
{
    if (m_backendsInFlight || !m_client.hasEndpoint())
        return;
    m_backendsInFlight = true;
    m_client.backends(this, [this, generation = m_generation](std::expected<QList<AiBackendInfo>, QString> answer) {
        m_backendsInFlight = false;
        if (generation != m_generation)
            return;
        if (answer)
            m_backends = std::move(*answer);
        else
            qCWarning(lcAi).noquote() << "Could not list the AI backends:" << answer.error();
    });
}

void AiJobTracker::ensureSidecar()
{
    if (m_sidecar && (m_sidecar->state() == SidecarState::Stopped || m_sidecar->state() == SidecarState::Failed))
        m_sidecar->start();
}

void AiJobTracker::onSidecarStateChanged(SidecarState state)
{
    if (state == SidecarState::Running) {
        m_client.setEndpoint(m_sidecar->baseUrl(), m_sidecar->token());
        refreshBackends();
        submitWaitingJobs();
        return;
    }

    ++m_generation;
    m_client.clearEndpoint();
    m_backends.reset();
    m_backendsInFlight = false;
    m_pollInFlight = false;
    m_pollTimer.stop();

    const bool givenUp = state == SidecarState::Failed || state == SidecarState::Stopped;
    const QString reason = state == SidecarState::Stopped || m_sidecar->errorString().isEmpty()
                               ? u"The AI service was stopped."_s
                               : m_sidecar->errorString();
    bool changed = false;
    for (Job& job : m_jobs) {
        if (isFinished(job.state))
            continue;
        if (!job.remoteId.isEmpty() || job.submitting) {
            finish(job, AiJobState::Failed, u"Failed"_s, kLostWithService);
            changed = true;
        } else if (givenUp) {
            finish(job, AiJobState::Failed, u"Failed"_s, reason);
            changed = true;
        } else if (job.message != kWaitingForService) {
            job.message = kWaitingForService;
            changed = true;
        }
    }
    if (changed)
        Q_EMIT jobsChanged();
}

void AiJobTracker::submitWaitingJobs()
{
    for (Job& job : m_jobs) {
        if (!isFinished(job.state) && job.remoteId.isEmpty() && !job.submitting)
            sendToSidecar(job);
    }
    Q_EMIT jobsChanged();
}

void AiJobTracker::sendToSidecar(Job& job)
{
    job.submitting = true;
    job.message = u"Sending to the AI service"_s;
    const QString id = job.id;
    m_client.submitJob(job.op, job.params, this,
                       [this, id, generation = m_generation](std::expected<QString, QString> answer) {
                           Job* sent = find(id);
                           if (!sent || generation != m_generation)
                               return;
                           sent->submitting = false;
                           if (!answer) {
                               if (!isFinished(sent->state))
                                   finish(*sent, AiJobState::Failed, u"Failed"_s, answer.error());
                               Q_EMIT jobsChanged();
                               return;
                           }
                           sent->remoteId = *answer;
                           if (sent->state == AiJobState::Canceled) {
                               m_client.cancelJob(sent->remoteId, this, [](std::expected<AiJobInfo, QString>) { });
                               return;
                           }
                           sent->message = u"Queued in the AI service"_s;
                           qCInfo(lcAi).noquote() << "AI job" << id << "is" << sent->remoteId << "in the sidecar";
                           if (!m_pollTimer.isActive())
                               m_pollTimer.start();
                           Q_EMIT jobsChanged();
                       });
}

void AiJobTracker::poll()
{
    if (!hasRemoteActiveJobs()) {
        m_pollTimer.stop();
        return;
    }
    if (m_pollInFlight || !m_client.hasEndpoint())
        return;
    m_pollInFlight = true;
    m_client.jobs(this, [this, generation = m_generation](std::expected<QList<AiJobInfo>, QString> answer) {
        if (generation != m_generation)
            return;
        m_pollInFlight = false;
        if (!answer) {
            qCWarning(lcAi).noquote() << "Could not read the AI jobs:" << answer.error();
            return;
        }
        for (Job& job : m_jobs) {
            if (isFinished(job.state) || job.remoteId.isEmpty())
                continue;
            const auto remote = std::ranges::find(*answer, job.remoteId, &AiJobInfo::id);
            if (remote == answer->end())
                finish(job, AiJobState::Failed, u"Failed"_s, kLostWithService);
            else
                apply(job, *remote);
        }
        Q_EMIT jobsChanged();
    });
}

void AiJobTracker::apply(Job& job, const AiJobInfo& remote)
{
    job.progress = remote.progress;
    if (!remote.message.isEmpty())
        job.message = remote.message;
    switch (remote.state) {
    case AiJobState::Queued:
    case AiJobState::Running:
        job.state = remote.state;
        return;
    case AiJobState::Failed:
        finish(job, AiJobState::Failed, u"Failed"_s, remote.error);
        return;
    case AiJobState::Canceled:
        finish(job, AiJobState::Canceled, u"Canceled"_s);
        return;
    case AiJobState::Succeeded:
        break;
    }

    job.result = remote.result;
    if (!job.followUp) {
        finish(job, AiJobState::Succeeded, u"Done"_s);
        return;
    }
    auto placed = job.followUp(remote);
    if (!placed) {
        finish(job, AiJobState::Failed, u"Failed"_s, placed.error());
        return;
    }
    job.placement = *placed;
    finish(job, AiJobState::Succeeded, u"Done"_s);
}

void AiJobTracker::finish(Job& job, AiJobState state, const QString& message, const QString& error)
{
    job.state = state;
    job.message = message;
    job.error = error;
    job.followUp = {};
    if (state == AiJobState::Succeeded)
        job.progress = 1.0;
    if (state == AiJobState::Failed)
        qCWarning(lcAi).noquote() << "AI job" << job.id << "failed:" << error;
    else
        qCInfo(lcAi).noquote() << "AI job" << job.id << toString(state);
}

AiJobTracker::Job* AiJobTracker::find(const QString& id)
{
    const auto it = std::ranges::find(m_jobs, id, &Job::id);
    return it == m_jobs.end() ? nullptr : &*it;
}

bool AiJobTracker::hasRemoteActiveJobs() const
{
    return std::ranges::any_of(m_jobs,
                               [](const Job& job) { return !isFinished(job.state) && !job.remoteId.isEmpty(); });
}

QJsonObject AiJobTracker::toJson(const Job& job)
{
    return QJsonObject{
        {u"id"_s, job.id},
        {u"remoteId"_s, job.remoteId},
        {u"op"_s, job.op},
        {u"title"_s, job.title},
        {u"params"_s, job.params},
        {u"state"_s, toString(job.state)},
        {u"progress"_s, job.progress},
        {u"message"_s, job.message},
        {u"error"_s, job.error},
        {u"createdAt"_s, job.createdAt.toString(Qt::ISODate)},
        {u"result"_s, job.result},
        {u"placement"_s, job.placement},
    };
}

}  // namespace mikserstudio::services
