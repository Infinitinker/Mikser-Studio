// Running jobs in lanes, publishing their changes, forgetting old ones, and the jobs.* commands.
#include "mikserstudio/core/JobManager.h"

#include "Logging.h"
#include "mikserstudio/core/CommandBus.h"

#include <QUuid>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

namespace {

bool isFinished(JobState state)
{
    return state == JobState::Succeeded || state == JobState::Failed || state == JobState::Canceled;
}

QJsonValue timeJson(const QDateTime& time)
{
    return time.isValid() ? QJsonValue(time.toString(Qt::ISODateWithMs)) : QJsonValue();
}

}  // namespace

QString toString(JobState state)
{
    switch (state) {
    case JobState::Queued:
        return u"queued"_s;
    case JobState::Running:
        return u"running"_s;
    case JobState::Succeeded:
        return u"succeeded"_s;
    case JobState::Failed:
        return u"failed"_s;
    case JobState::Canceled:
        return u"canceled"_s;
    }
    return u"failed"_s;
}

JobManager::JobManager(CommandBus& bus, QObject* parent)
    : QObject(parent)
    , m_bus(bus)
{
}

JobManager::~JobManager() = default;

std::expected<QString, CommandError> JobManager::submit(JobSpec spec, JobRun run, JobCancel cancel)
{
    if (spec.id.isEmpty())
        spec.id = u"job-"_s + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (find(spec.id)) {
        return std::unexpected(CommandError{CommandErrorCode::Failed,
                                            u"There is a job “%1” already. Report this problem."_s.arg(spec.id)});
    }
    const CallContext* running = m_bus.runningContext();
    Job& job = m_jobs.emplace_back();
    job.origins = spec.origins.value_or(running ? running->origins : QStringList());
    job.spec = std::move(spec);
    job.run = std::move(run);
    job.cancel = std::move(cancel);
    job.createdAt = QDateTime::currentDateTimeUtc();
    QString id = job.spec.id;
    const QString lane = job.spec.lane;
    qCInfo(lcJobs).noquote() << "Job" << id << "(" << job.spec.kind << ") queued in lane" << lane;
    publish(
        job, u"job.created"_s,
        QJsonObject{
            {u"kind"_s, job.spec.kind}, {u"title"_s, job.spec.title}, {u"owner"_s, job.spec.owner}, {u"lane"_s, lane}});
    Q_EMIT jobsChanged();
    if (lane.isEmpty()) {
        Job* now = find(id);
        now->state = JobState::Running;
        now->startedAt = QDateTime::currentDateTimeUtc();
        if (now->run)
            now->run(id);
    } else {
        startNext(lane);
    }
    return id;
}

void JobManager::registerKind(const QString& kind, JobKind jobKind)
{
    m_kinds.insert_or_assign(kind, std::move(jobKind));
}

std::expected<QString, CommandError> JobManager::start(const QString& kind, const QJsonObject& input,
                                                       const CallContext& context)
{
    const auto it = m_kinds.find(kind);
    if (it == m_kinds.end())
        return std::unexpected(
            CommandError{CommandErrorCode::InvalidParams, u"There is no job kind “%1”."_s.arg(kind)});
    if (!it->second.permission.isEmpty() && !context.permissions.contains(it->second.permission)) {
        return std::unexpected(
            CommandError{CommandErrorCode::PermissionDenied,
                         u"Starting a “%1” job needs the “%2” permission, which the caller doesn't have."_s.arg(
                             kind, it->second.permission)});
    }
    const QString& caller = context.origins.constLast();
    JobSpec spec{
        .id = {},
        .kind = kind,
        .title = kind,
        .owner = caller.startsWith(u"plugin:"_s) ? caller.mid(7) : u"core"_s,
        .lane = {},
        .origins = context.origins,
    };
    // Copied: the kind may register kinds while it starts.
    const JobKind jobKind = it->second;
    return jobKind.start(*this, std::move(spec), input);
}

void JobManager::progress(const QString& jobId, double progress, const QString& message)
{
    Job* job = find(jobId);
    if (!job || job->state != JobState::Running)
        return;
    job->progress = std::clamp(progress, 0.0, 1.0);
    job->message = message;
    Q_EMIT jobsChanged();
    if (job->sinceProgress.isValid() && job->sinceProgress.durationElapsed() < kProgressInterval)
        return;
    job->sinceProgress.start();
    publish(*job, u"job.progress"_s, QJsonObject{{u"progress"_s, job->progress}, {u"message"_s, job->message}});
}

void JobManager::succeed(const QString& jobId, const QJsonObject& result)
{
    if (Job* job = find(jobId); job && !isFinished(job->state))
        finish(*job, JobState::Succeeded, {}, result);
}

void JobManager::fail(const QString& jobId, const QString& error)
{
    if (Job* job = find(jobId); job && !isFinished(job->state))
        finish(*job, JobState::Failed, error, {});
}

std::expected<JobState, CommandError> JobManager::cancel(const QString& jobId)
{
    Job* job = find(jobId);
    if (!job) {
        return std::unexpected(CommandError{
            CommandErrorCode::InvalidParams,
            u"There is no job “%1”. Call jobs.list to see the jobs; finished ones are forgotten after %2."_s.arg(jobId)
                .arg(kMaxFinishedJobs)});
    }
    if (isFinished(job->state))
        return job->state;
    const JobCancel stop = job->cancel;
    finish(*job, JobState::Canceled, {}, {});
    if (stop)
        stop(jobId);
    return JobState::Canceled;
}

QJsonArray JobManager::jobsJson() const
{
    QJsonArray jobs;
    for (const Job& job : m_jobs)
        jobs.append(toJson(job));
    return jobs;
}

std::optional<JobState> JobManager::state(const QString& jobId) const
{
    const Job* job = find(jobId);
    return job ? std::optional(job->state) : std::nullopt;
}

JobManager::Job* JobManager::find(const QString& jobId)
{
    const auto it = std::ranges::find(m_jobs, jobId, [](const Job& job) { return job.spec.id; });
    return it == m_jobs.end() ? nullptr : &*it;
}

const JobManager::Job* JobManager::find(const QString& jobId) const
{
    const auto it = std::ranges::find(m_jobs, jobId, [](const Job& job) { return job.spec.id; });
    return it == m_jobs.end() ? nullptr : &*it;
}

void JobManager::startNext(const QString& lane)
{
    if (m_running.contains(lane))
        return;
    const auto next = std::ranges::find_if(
        m_jobs, [&lane](const Job& job) { return job.spec.lane == lane && job.state == JobState::Queued; });
    if (next == m_jobs.end())
        return;
    const QString id = next->spec.id;
    m_running.emplace(lane, id);
    next->state = JobState::Running;
    next->startedAt = QDateTime::currentDateTimeUtc();
    qCInfo(lcJobs).noquote() << "Job" << id << "started";
    Q_EMIT jobsChanged();
    // Copied: the work may finish the job, and start the next one, before this returns.
    const JobRun run = next->run;
    if (run)
        run(id);
}

void JobManager::finish(Job& job, JobState state, const QString& error, const QJsonObject& result)
{
    job.state = state;
    job.error = error;
    job.result = result;
    job.finishedAt = QDateTime::currentDateTimeUtc();
    if (state == JobState::Succeeded)
        job.progress = 1.0;
    job.run = nullptr;
    job.cancel = nullptr;
    const QString id = job.spec.id;
    const QString lane = job.spec.lane;
    if (const auto running = m_running.find(lane); running != m_running.end() && running->second == id)
        m_running.erase(running);
    qCInfo(lcJobs).noquote() << "Job" << id << toString(state) << (error.isEmpty() ? QString() : error);
    publish(job, u"job.finished"_s,
            QJsonObject{{u"state"_s, toString(state)}, {u"error"_s, error}, {u"result"_s, result}});
    forgetOldJobs();
    Q_EMIT jobsChanged();
    if (!lane.isEmpty())
        startNext(lane);
}

void JobManager::forgetOldJobs()
{
    const auto finished = static_cast<std::size_t>(
        std::ranges::count_if(m_jobs, [](const Job& job) { return isFinished(job.state); }));
    std::size_t extra = finished > kMaxFinishedJobs ? finished - kMaxFinishedJobs : 0;
    for (auto it = m_jobs.begin(); extra > 0 && it != m_jobs.end();) {
        if (isFinished(it->state)) {
            it = m_jobs.erase(it);
            --extra;
        } else {
            ++it;
        }
    }
}

void JobManager::publish(const Job& job, const QString& topic, const QJsonObject& payload)
{
    QJsonObject event = payload;
    event.insert(u"jobId"_s, job.spec.id);
    m_bus.events().publish({topic, event, job.origins});
}

QJsonObject JobManager::toJson(const Job& job)
{
    return QJsonObject{
        {u"id"_s, job.spec.id},
        {u"kind"_s, job.spec.kind},
        {u"title"_s, job.spec.title},
        {u"owner"_s, job.spec.owner},
        {u"lane"_s, job.spec.lane},
        {u"state"_s, toString(job.state)},
        {u"progress"_s, job.progress},
        {u"message"_s, job.message},
        {u"error"_s, job.error},
        {u"createdAt"_s, timeJson(job.createdAt)},
        {u"startedAt"_s, timeJson(job.startedAt)},
        {u"finishedAt"_s, timeJson(job.finishedAt)},
        {u"result"_s, job.result},
        {u"origins"_s, QJsonArray::fromStringList(job.origins)},
    };
}

void registerJobCommands(CommandBus& bus, JobManager& jobs)
{
    bus.registerCommand(
        CommandSpec{
            .id = u"jobs.list"_s,
            .title = u"List jobs"_s,
            .description = u"Lists the jobs: the ones queued or running and the last finished ones, oldest first."_s,
            .category = u"Jobs"_s,
            .paramsSchema = {},
            .shortcut = {},
            .undoable = false,
            .readOnly = true,
            .confirmation = {},
            .confirmationParam = {},
            .owner = u"core"_s,
            .resultSchema = objectResultSchema({{u"jobs"_s, u"array"_s}}),
        },
        [&jobs](const QJsonObject&) -> CommandResult {
            return CommandOutcome{QJsonObject{{u"jobs"_s, jobs.jobsJson()}}, nullptr};
        });
    bus.registerCommand(
        CommandSpec{
            .id = u"jobs.cancel"_s,
            .title = u"Cancel job"_s,
            .description = u"Cancels a job that hasn't finished and answers the state it has now."_s,
            .category = u"Jobs"_s,
            .paramsSchema = QJsonObject{{u"type"_s, u"object"_s},
                                        {u"properties"_s, QJsonObject{{u"jobId"_s, QJsonObject{{u"type"_s, u"string"_s},
                                                                                               {u"description"_s,
                                                                                                u"The job's id."_s}}}}},
                                        {u"required"_s, QJsonArray{u"jobId"_s}}},
            .shortcut = {},
            .undoable = false,
            .readOnly = false,
            .confirmation = {},
            .confirmationParam = {},
            .owner = u"core"_s,
            .resultSchema = objectResultSchema({{u"state"_s, u"string"_s}}),
        },
        [&jobs](const QJsonObject& params) -> CommandResult {
            const auto state = jobs.cancel(params.value(u"jobId"_s).toString());
            if (!state)
                return std::unexpected(state.error());
            return CommandOutcome{QJsonObject{{u"state"_s, toString(*state)}}, nullptr};
        });
}

}  // namespace mikserstudio::core
