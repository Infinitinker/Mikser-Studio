// Queuing, running, canceling and finishing AI jobs, one at a time per backend, and keeping each backend's
// availability.
#include "mikserstudio/services/AiEngine.h"

#include "AiBackend.h"
#include "ComfyUiBackend.h"
#include "Logging.h"
#include "OllamaBackend.h"
#include "mikserstudio/core/JsonSchema.h"
#include "mikserstudio/core/Project.h"

#include <QDir>

#include <algorithm>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace mikserstudio::services {

namespace {

// How long a check of a backend's availability counts as current.
constexpr auto kAvailabilityMaxAge = 5s;

QJsonArray outputsJson(const QList<AiOutput>& outputs)
{
    QJsonArray list;
    for (const AiOutput& output : outputs)
        list.append(QJsonObject{{u"path"_s, output.path}, {u"kind"_s, output.kind}});
    return list;
}

QString timestamp(const QDateTime& time)
{
    return time.isValid() ? time.toString(Qt::ISODate) : QString();
}

}  // namespace

struct AiEngine::Job {
    QString id;
    QString op;
    QString backend;
    QString title;
    QJsonObject params;
    AiJobState state = AiJobState::Queued;
    double progress = 0.0;
    QString message;
    QString error;
    QDateTime createdAt;
    QDateTime startedAt;
    QDateTime finishedAt;
    QJsonObject result;
    QJsonObject placement;
    FollowUp followUp;
    std::unique_ptr<AiTask> task;
};

struct AiEngine::Slot {
    std::unique_ptr<AiBackend> backend;
    std::optional<AiAvailability> availability;
    QDateTime checkedAt;
    bool checking = false;
    quint64 generation = 0;  // Bumped when the backend's address changes, so older checks are ignored.
    QString runningJob;
};

AiEngine::AiEngine(AiEngineOptions options, QObject* parent)
    : QObject(parent)
    , m_options(std::move(options))
    , m_settings(loadAiSettings(m_options.settingsFile))
{
    addBackend(std::make_unique<OllamaBackend>(m_settings.ollamaUrl));
    addBackend(std::make_unique<ComfyUiBackend>(m_settings.comfyuiUrl));
}

AiEngine::~AiEngine()
{
    // Running tasks take their own prompts off the tools' queues; their work in the tools goes on.
    for (const auto& job : m_jobs) {
        if (job->task)
            job->task->cancel();
    }
    m_jobs.clear();
}

void AiEngine::addBackend(std::unique_ptr<AiBackend> backend)
{
    auto slot = std::make_unique<Slot>();
    slot->backend = std::move(backend);
    m_slots.push_back(std::move(slot));
}

std::expected<void, QString> AiEngine::setSettings(const AiSettings& settings)
{
    if (auto saved = saveAiSettings(settings, m_options.settingsFile); !saved)
        return saved;
    const std::pair<QString, QUrl> addresses[] = {
        {u"ollama"_s, settings.ollamaUrl},
        {u"comfyui"_s, settings.comfyuiUrl},
    };
    const std::pair<QString, QUrl> old[] = {
        {u"ollama"_s, m_settings.ollamaUrl},
        {u"comfyui"_s, m_settings.comfyuiUrl},
    };
    m_settings = settings;
    for (std::size_t i = 0; i < std::size(addresses); ++i) {
        if (addresses[i].second == old[i].second)
            continue;
        if (Slot* slot = slotFor(addresses[i].first)) {
            slot->backend->setBaseUrl(addresses[i].second);
            ++slot->generation;
            slot->availability.reset();
            slot->checking = false;
        }
        qCInfo(lcAi).noquote() << "The" << addresses[i].first << "backend now connects to"
                               << addresses[i].second.toString();
    }
    Q_EMIT backendsChanged();
    return {};
}

QList<AiBackendInfo> AiEngine::backends() const
{
    QList<AiBackendInfo> list;
    for (const auto& slot : m_slots) {
        AiBackendInfo info{
            .id = slot->backend->id(),
            .title = slot->backend->title(),
            .operations = slot->backend->operations(),
            .available = false,
            .reason = {},
        };
        if (slot->availability) {
            info.available = slot->availability->available;
            info.reason = slot->availability->reason;
        } else {
            info.reason = u"Mikser Studio is checking whether %1 is running. Ask again in a moment."_s.arg(info.title);
        }
        list.append(info);
    }
    return list;
}

void AiEngine::refreshAvailability()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const auto& owned : m_slots) {
        Slot* slot = owned.get();
        if (slot->checking || (slot->checkedAt.isValid() && slot->availability &&
                               slot->checkedAt.msecsTo(now) < std::chrono::milliseconds(kAvailabilityMaxAge).count())) {
            continue;
        }
        slot->checking = true;
        slot->backend->checkAvailability(
            this, [this, slot, generation = slot->generation](AiAvailability availability) {
                if (generation != slot->generation)
                    return;
                slot->checking = false;
                slot->checkedAt = QDateTime::currentDateTimeUtc();
                const bool changed = !slot->availability || slot->availability->available != availability.available ||
                                     slot->availability->reason != availability.reason;
                slot->availability = std::move(availability);
                if (changed)
                    Q_EMIT backendsChanged();
            });
    }
}

QStringList AiEngine::operationIds() const
{
    QStringList ids;
    for (const auto& slot : m_slots) {
        for (const AiOperation& operation : slot->backend->operations()) {
            if (!ids.contains(operation.id))
                ids.append(operation.id);
        }
    }
    return ids;
}

std::expected<QString, core::CommandError> AiEngine::submit(const QString& op, const QJsonObject& params,
                                                            const QString& backendId, const QString& subject,
                                                            FollowUp followUp)
{
    using core::CommandErrorCode;
    if (!backendId.isEmpty() && !slotFor(backendId)) {
        return core::commandFailure(
            CommandErrorCode::InvalidParams,
            u"There is no AI backend “%1”. Run ai.backends to see the backends."_s.arg(backendId));
    }
    Slot* chosen = nullptr;
    AiOperation operation;
    for (const auto& slot : m_slots) {
        if (!backendId.isEmpty() && slot->backend->id() != backendId)
            continue;
        const auto ops = slot->backend->operations();
        const auto found = std::ranges::find(ops, op, &AiOperation::id);
        if (found == ops.end())
            continue;
        const bool available = slot->availability && slot->availability->available;
        if (!chosen || (available && !(chosen->availability && chosen->availability->available))) {
            chosen = slot.get();
            operation = *found;
        }
    }
    if (!chosen) {
        return core::commandFailure(
            CommandErrorCode::InvalidParams,
            backendId.isEmpty()
                ? u"No AI backend offers the operation “%1”. Run ai.backends to see the operations."_s.arg(op)
                : u"The AI backend “%1” does not offer the operation “%2”. Run ai.backends to see the operations."_s
                      .arg(backendId, op));
    }

    const QJsonObject complete = core::JsonSchema::applyDefaults(params, operation.paramsSchema);
    if (const QStringList problems = core::JsonSchema::validate(complete, operation.paramsSchema);
        !problems.isEmpty()) {
        return core::commandFailure(CommandErrorCode::InvalidParams,
                                    u"Invalid parameters for “%1”: %2"_s.arg(op, problems.join(u"; "_s)));
    }

    auto job = std::make_unique<Job>();
    job->id = core::ProjectModel::newId(u"aijob"_s);
    job->op = op;
    job->backend = chosen->backend->id();
    job->title = subject.isEmpty() ? operation.title : u"%1: %2"_s.arg(operation.title, subject);
    job->params = complete;
    job->message = u"Waiting for other jobs to finish"_s;
    job->createdAt = QDateTime::currentDateTimeUtc();
    job->followUp = std::move(followUp);
    QString id = job->id;
    qCInfo(lcAi).noquote() << "New AI job" << id << op << "on" << job->backend << "-" << job->title;
    m_jobs.push_back(std::move(job));
    startNext(*chosen);
    Q_EMIT jobsChanged();
    return id;
}

std::expected<AiJobState, core::CommandError> AiEngine::cancel(const QString& jobId)
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
    const bool running = job->state == AiJobState::Running;
    if (running) {
        job->task->cancel();
        retireTask(*job);
    }
    finish(*job, AiJobState::Canceled, u"Canceled"_s);
    if (running) {
        QDir(jobFolder(jobId)).removeRecursively();
        if (Slot* slot = slotFor(job->backend)) {
            slot->runningJob.clear();
            startNext(*slot);
        }
    }
    Q_EMIT jobsChanged();
    return AiJobState::Canceled;
}

QJsonArray AiEngine::jobsJson() const
{
    QJsonArray jobs;
    for (const auto& job : m_jobs)
        jobs.append(toJson(*job));
    return jobs;
}

int AiEngine::activeJobCount() const
{
    return static_cast<int>(std::ranges::count_if(m_jobs, [](const auto& job) { return !isFinished(job->state); }));
}

void AiEngine::startNext(Slot& slot)
{
    while (slot.runningJob.isEmpty()) {
        const QString backendId = slot.backend->id();
        const auto next = std::ranges::find_if(m_jobs, [&backendId](const auto& job) {
            return job->backend == backendId && job->state == AiJobState::Queued;
        });
        if (next == m_jobs.end())
            return;
        Job& job = **next;
        const QString folder = jobFolder(job.id);
        if (!QDir().mkpath(folder)) {
            finish(job, AiJobState::Failed, u"Failed"_s,
                   u"The AI job could not create its folder “%1”. Check that the cache folder can be written to."_s.arg(
                       QDir::toNativeSeparators(folder)));
            continue;
        }
        job.state = AiJobState::Running;
        job.startedAt = QDateTime::currentDateTimeUtc();
        job.message = u"Starting"_s;
        slot.runningJob = job.id;
        const QString id = job.id;
        job.task = slot.backend->start(
            job.op, job.params,
            AiTaskContext{
                .jobId = id,
                .outputDirectory = folder,
                .report =
                    [this, id](double progress, const QString& message) {
                        Job* reported = find(id);
                        if (!reported || reported->state != AiJobState::Running)
                            return;
                        reported->progress = std::clamp(progress, 0.0, 1.0);
                        if (!message.isEmpty())
                            reported->message = message;
                        Q_EMIT jobsChanged();
                    },
                .finish = [this,
                           id](std::expected<AiOutcome, QString> outcome) { taskFinished(id, std::move(outcome)); },
            });
        qCInfo(lcAi).noquote() << "AI job" << id << "started";
    }
}

void AiEngine::taskFinished(const QString& jobId, std::expected<AiOutcome, QString> outcome)
{
    Job* job = find(jobId);
    if (!job || job->state != AiJobState::Running)
        return;
    retireTask(*job);
    Slot* slot = slotFor(job->backend);
    if (slot)
        slot->runningJob.clear();

    if (!outcome) {
        QDir(jobFolder(jobId)).removeRecursively();
        finish(*job, AiJobState::Failed, u"Failed"_s, outcome.error());
    } else {
        QJsonObject params = job->params;
        // A seed the backend picked belongs to the parameters the result can be made again with.
        if (outcome->data.contains(u"seed") && !params.contains(u"seed"))
            params.insert(u"seed"_s, outcome->data.value(u"seed"));
        QJsonObject provenance = outcome->provenance;
        provenance.insert(u"jobId"_s, job->id);
        provenance.insert(u"backend"_s, job->backend);
        provenance.insert(u"op"_s, job->op);
        provenance.insert(u"params"_s, params);
        provenance.insert(u"createdAt"_s, timestamp(QDateTime::currentDateTimeUtc()));
        provenance.insert(u"app"_s, QJsonObject{{u"name"_s, u"Mikser Studio"_s},
                                                {u"version"_s, QStringLiteral(MIKSERSTUDIO_VERSION_STRING)}});
        QJsonObject result = outcome->data;
        result.insert(u"outputs"_s, outputsJson(outcome->outputs));
        result.insert(u"provenance"_s, provenance);
        job->result = std::move(result);
        job->progress = 1.0;
        QDir().rmdir(jobFolder(jobId));  // Only when the job left nothing in it, as a text job does.

        if (job->followUp) {
            auto placed = job->followUp(toJson(*job));
            if (placed) {
                job->placement = *placed;
                finish(*job, AiJobState::Succeeded, u"Done"_s);
            } else {
                finish(*job, AiJobState::Failed, u"Failed"_s, placed.error());
            }
        } else {
            finish(*job, AiJobState::Succeeded, u"Done"_s);
        }
    }
    if (slot)
        startNext(*slot);
    forgetOldJobs();
    Q_EMIT jobsChanged();
}

void AiEngine::finish(Job& job, AiJobState state, const QString& message, const QString& error)
{
    job.state = state;
    job.message = message;
    job.error = error;
    job.finishedAt = QDateTime::currentDateTimeUtc();
    job.followUp = {};
    if (state == AiJobState::Succeeded)
        job.progress = 1.0;
    if (state == AiJobState::Failed)
        qCWarning(lcAi).noquote() << "AI job" << job.id << "failed:" << error;
    else
        qCInfo(lcAi).noquote() << "AI job" << job.id << toString(state);
}

void AiEngine::retireTask(Job& job)
{
    // The task may be the one calling: it goes when control is back in the event loop.
    if (AiTask* task = job.task.release()) {
        task->setParent(this);
        task->deleteLater();
    }
}

void AiEngine::forgetOldJobs()
{
    auto finished = std::ranges::count_if(m_jobs, [](const auto& job) { return isFinished(job->state); });
    for (auto it = m_jobs.begin(); it != m_jobs.end() && finished > kMaxFinishedJobs;) {
        if (isFinished((*it)->state)) {
            it = m_jobs.erase(it);
            --finished;
        } else {
            ++it;
        }
    }
}

AiEngine::Job* AiEngine::find(const QString& id) const
{
    const auto it = std::ranges::find(m_jobs, id, [](const auto& job) { return job->id; });
    return it == m_jobs.end() ? nullptr : it->get();
}

AiEngine::Slot* AiEngine::slotFor(const QString& backendId) const
{
    const auto it = std::ranges::find_if(m_slots,
                                         [&backendId](const auto& slot) { return slot->backend->id() == backendId; });
    return it == m_slots.end() ? nullptr : it->get();
}

QString AiEngine::jobFolder(const QString& jobId) const
{
    return QDir(m_options.cacheDirectory).filePath(jobId);
}

QJsonObject AiEngine::toJson(const Job& job)
{
    return QJsonObject{
        {u"id"_s, job.id},
        {u"op"_s, job.op},
        {u"backend"_s, job.backend},
        {u"title"_s, job.title},
        {u"params"_s, job.params},
        {u"state"_s, toString(job.state)},
        {u"progress"_s, job.progress},
        {u"message"_s, job.message},
        {u"error"_s, job.error},
        {u"createdAt"_s, timestamp(job.createdAt)},
        {u"startedAt"_s, timestamp(job.startedAt)},
        {u"finishedAt"_s, timestamp(job.finishedAt)},
        {u"result"_s, job.result},
        {u"placement"_s, job.placement},
    };
}

}  // namespace mikserstudio::services
