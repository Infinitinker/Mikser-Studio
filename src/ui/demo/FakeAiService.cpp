// The stand-in AI commands: registering them and advancing their fake jobs.
#include "FakeAiService.h"

#include "mikserstudio/core/CommandBus.h"

#include <QJsonObject>
#include <QUuid>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui::demo {

namespace {

constexpr int kTickMs = 250;
constexpr double kProgressPerTick = 0.02;
constexpr qsizetype kTitleChars = 40;

const auto kImageOp = u"image.generate"_s;
const auto kTextOp = u"text.generate"_s;

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

// The backend that runs `op` in the real app.
QString backendOf(const QString& op)
{
    return op == kImageOp ? u"comfyui"_s : u"ollama"_s;
}

}  // namespace

FakeAiService::FakeAiService(core::CommandBus& bus, QObject* parent)
    : QObject(parent)
    , m_bus(bus)
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
    add(aiSpec(
            u"ai.run"_s, u"Run AI operation"_s,
            u"Runs an AI operation, such as image.generate or text.generate, in the AI tool of its backend; images, video and audio it makes come into the project."_s,
            QJsonObject{
                {u"op"_s, QJsonObject{{u"type"_s, u"string"_s},
                                      {u"enum"_s, QJsonArray{kImageOp, kTextOp}},
                                      {u"description"_s, u"The operation, from ai.backends."_s}}},
                {u"params"_s,
                 QJsonObject{{u"type"_s, u"object"_s},
                             {u"default"_s, QJsonObject{}},
                             {u"description"_s, u"The operation's parameters, as its schema in ai.backends says."_s}}},
                {u"backend"_s, stringParam(u"The backend to run it on; the first that offers it when omitted."_s)}},
            {u"op"_s}),
        [this](const QJsonObject& params) -> core::CommandResult {
            const QString op = params.value(u"op").toString();
            QString prompt = params.value(u"params").toObject().value(u"prompt").toString().simplified();
            if (prompt.isEmpty()) {
                return core::commandFailure(core::CommandErrorCode::InvalidParams,
                                            u"Invalid parameters for “%1”: “prompt” is required."_s.arg(op));
            }
            if (prompt.size() > kTitleChars)
                prompt = prompt.left(kTitleChars - 1) + u"…"_s;
            const QString title = op == kImageOp ? u"Generate image: %1"_s.arg(prompt)
                                                 : u"Generate text: %1"_s.arg(prompt);
            return ok(QJsonObject{{u"jobId"_s, submit(op, title, 0.0)}});
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
    if (job.state == u"succeeded" && op == kTextOp)
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
        job.message = job.op == kImageOp ? u"Sampling (demo)"_s : u"Thinking (demo)"_s;
        if (job.progress < 1.0)
            continue;
        job.state = u"succeeded"_s;
        if (job.op == kTextOp) {
            job.message = u"Done"_s;
            job.resultText = u"This is the demo AI; the real answer comes from the local model."_s;
        } else {
            job.message = u"Demo only: no image was made."_s;
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
            {u"backend"_s, backendOf(job.op)},
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
