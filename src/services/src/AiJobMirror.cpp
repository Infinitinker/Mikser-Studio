// The AI engine's jobs as core jobs: each one is mirrored in the JobManager, so jobs.list,
// jobs.cancel and the job.* events cover them, until the backends move to plugins that use the
// JobManager themselves.
#include "mikserstudio/services/AiCommands.h"

#include "mikserstudio/core/JobManager.h"
#include "mikserstudio/services/AiEngine.h"

#include <QJsonArray>

#include <memory>
#include <set>
#include <tuple>

using namespace Qt::StringLiterals;

namespace mikserstudio::services {

void mirrorAiJobs(AiEngine& ai, core::JobManager& jobs)
{
    // The AI jobs mirrored so far, so that one the JobManager has forgotten isn't mirrored again.
    auto mirrored = std::make_shared<std::set<QString>>();
    QObject::connect(&ai, &AiEngine::jobsChanged, &jobs, [&ai, &jobs, mirrored] {
        std::set<QString> current;
        for (const QJsonValue value : ai.jobsJson()) {
            const QJsonObject job = value.toObject();
            const QString id = job.value(u"id"_s).toString();
            current.insert(id);
            if (!mirrored->contains(id)) {
                mirrored->insert(id);
                // The engine runs one job per backend at a time, in order, as a lane does.
                std::ignore = jobs.submit(
                    core::JobSpec{
                        .id = id,
                        .kind = u"ai."_s + job.value(u"op"_s).toString(),
                        .title = job.value(u"title"_s).toString(),
                        .owner = u"core"_s,
                        .lane = u"ai:"_s + job.value(u"backend"_s).toString(),
                    },
                    {}, [&ai](const QString& jobId) { std::ignore = ai.cancel(jobId); });
            }
            const std::optional<AiJobState> state = aiJobStateFromString(job.value(u"state"_s).toString());
            if (!state)
                continue;
            switch (*state) {
            case AiJobState::Queued:
                break;
            case AiJobState::Running:
                jobs.progress(id, job.value(u"progress"_s).toDouble(), job.value(u"message"_s).toString());
                break;
            case AiJobState::Succeeded:
                jobs.succeed(id, QJsonObject{{u"result"_s, job.value(u"result"_s)},
                                             {u"placement"_s, job.value(u"placement"_s)}});
                break;
            case AiJobState::Failed:
                jobs.fail(id, job.value(u"error"_s).toString());
                break;
            case AiJobState::Canceled:
                std::ignore = jobs.cancel(id);
                break;
            }
        }
        std::erase_if(*mirrored, [&current](const QString& id) { return !current.contains(id); });
    });
}

}  // namespace mikserstudio::services
