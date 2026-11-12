// The stand-in AI commands: registering them and advancing their fake jobs.
#include "FakeAiService.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/Project.h"

#include <QJsonObject>
#include <QUuid>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui::demo {

namespace {

constexpr int kTickMs = 250;
constexpr double kProgressPerTick = 0.02;

QJsonObject stringParam(const QString& description)
{
    return {{u"type"_s, u"string"_s}, {u"minLength"_s, 1}, {u"description"_s, description}};
}

core::CommandSpec aiSpec(const QString& id, const QString& title, const QString& description,
                         QJsonObject properties = {}, const QStringList& required = {})
{
    return core::CommandSpec{
        .id = id,
        .title = title,
        .description = description,
        .category = u"AI"_s,
        .paramsSchema = QJsonObject{{u"type"_s, u"object"_s},
                                    {u"properties"_s, properties},
                                    {u"required"_s, QJsonArray::fromStringList(required)},
                                    {u"additionalProperties"_s, false}},
        .shortcut = {},
        .undoable = false,
    };
}

core::CommandResult ok(QJsonObject result)
{
    return core::CommandOutcome{std::move(result), nullptr};
}

}  // namespace

FakeAiService::FakeAiService(core::CommandBus& bus, const core::ProjectModel& project, QObject* parent)
    : QObject(parent)
    , m_bus(bus)
    , m_project(project)
{
    const auto add = [this](core::CommandSpec spec, core::CommandHandler handler) {
        const QString id = spec.id;
        if (m_bus.registerCommand(std::move(spec), std::move(handler)))
            m_commandIds.append(id);
    };
    core::CommandSpec jobs = aiSpec(u"ai.jobs"_s, u"List AI jobs"_s,
                                    u"Lists the AI jobs of this session with their progress and results."_s);
    jobs.readOnly = true;
    add(std::move(jobs), [this](const QJsonObject&) { return ok(QJsonObject{{u"jobs"_s, jobsJson()}}); });
    add(aiSpec(u"ai.cancel_job"_s, u"Cancel AI job"_s, u"Stops a queued or running AI job."_s,
               QJsonObject{{u"jobId"_s, stringParam(u"Id of the job, from ai.jobs."_s)}}, {u"jobId"_s}),
        [this](const QJsonObject& params) -> core::CommandResult {
            const QString id = params.value(u"jobId").toString();
            for (Job& job : m_jobs) {
                if (job.id != id)
                    continue;
                if (job.state == u"queued" || job.state == u"running") {
                    job.state = u"canceled"_s;
                    job.message = u"Canceled"_s;
                }
                return ok(QJsonObject{{u"jobId"_s, id}, {u"state"_s, job.state}});
            }
            return core::commandFailure(core::CommandErrorCode::Failed, u"There is no AI job “%1”."_s.arg(id));
        });
    add(aiSpec(u"ai.upscale_clip"_s, u"Upscale clip"_s,
               u"Upscales a video clip and places the result on the video track above it."_s,
               QJsonObject{{u"clipId"_s, stringParam(u"Id of the video clip to upscale."_s)},
                           {u"scale"_s, QJsonObject{{u"type"_s, u"integer"_s},
                                                    {u"enum"_s, QJsonArray{2, 3, 4}},
                                                    {u"default"_s, 2},
                                                    {u"description"_s, u"Scale factor."_s}}}},
               {u"clipId"_s}),
        [this](const QJsonObject& params) -> core::CommandResult {
            const core::Clip* clip = m_project.findClip(params.value(u"clipId").toString());
            const core::MediaItem* media = clip ? m_project.findMedia(clip->mediaId) : nullptr;
            if (!media || media->info.kind != core::MediaKind::Video)
                return core::commandFailure(core::CommandErrorCode::Failed, u"Select a video clip to upscale."_s);
            const QString title = u"Upscale “%1” x%2"_s.arg(media->name).arg(params.value(u"scale").toInt());
            return ok(QJsonObject{{u"jobId"_s, submit(u"video.upscale"_s, title, 0.0)}});
        });
    add(aiSpec(u"ai.generate_text"_s, u"Generate text"_s, u"Answers a prompt with a local language model."_s,
               QJsonObject{{u"prompt"_s, stringParam(u"What to ask the model."_s)},
                           {u"model"_s, stringParam(u"Model name; the first installed model when omitted."_s)}},
               {u"prompt"_s}),
        [this](const QJsonObject& params) {
            QString prompt = params.value(u"prompt").toString();
            if (prompt.size() > 40)
                prompt = prompt.left(39) + u"…"_s;
            return ok(QJsonObject{{u"jobId"_s, submit(u"text.generate"_s, u"Generate text: %1"_s.arg(prompt), 0.0)}});
        });

    m_timer.setInterval(kTickMs);
    connect(&m_timer, &QTimer::timeout, this, &FakeAiService::tick);
    m_timer.start();
}

FakeAiService::~FakeAiService()
{
    for (const QString& id : std::as_const(m_commandIds))
        m_bus.unregisterCommand(id);
}

QString FakeAiService::submit(const QString& op, const QString& title, double progress)
{
    Job job;
    job.id = u"aijob-"_s + QUuid::createUuid().toString(QUuid::WithoutBraces);
    job.op = op;
    job.title = title;
    job.progress = std::clamp(progress, 0.0, 1.0);
    job.state = job.progress >= 1.0 ? u"succeeded"_s : job.progress > 0.0 ? u"running"_s : u"queued"_s;
    if (job.state == u"succeeded" && op == u"text.generate")
        job.resultText = u"A calm, slow push-in on the interview, then cut to the city at dusk."_s;
    m_jobs.append(job);
    return job.id;
}

void FakeAiService::tick()
{
    for (Job& job : m_jobs) {
        if (job.state == u"queued") {
            job.state = u"running"_s;
            continue;
        }
        if (job.state != u"running")
            continue;
        job.progress = std::min(1.0, job.progress + kProgressPerTick);
        job.message = job.op == u"video.upscale" ? u"Upscaling frames (demo)"_s : u"Thinking (demo)"_s;
        if (job.progress < 1.0)
            continue;
        job.state = u"succeeded"_s;
        if (job.op == u"text.generate") {
            job.message = u"Done"_s;
            job.resultText = u"This is the demo AI service; the real answer comes from the local model."_s;
        } else {
            job.message = u"Demo only: no upscaled file was produced."_s;
        }
    }
}

QJsonArray FakeAiService::jobsJson() const
{
    QJsonArray list;
    for (const Job& job : m_jobs) {
        QJsonObject result;
        if (!job.resultText.isEmpty())
            result.insert(u"text"_s, job.resultText);
        list.append(QJsonObject{
            {u"id"_s, job.id},
            {u"op"_s, job.op},
            {u"title"_s, job.title},
            {u"state"_s, job.state},
            {u"progress"_s, job.progress},
            {u"message"_s, job.message},
            {u"error"_s, QString()},
            {u"result"_s, result},
        });
    }
    return list;
}

}  // namespace mikserstudio::ui::demo
