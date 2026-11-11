#include "mikserstudio/services/AiCommands.h"

#include "AiJobTracker.h"
#include "AiJson.h"
#include "UpscalePlacement.h"
#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/services/AiClient.h"
#include "mikserstudio/services/SidecarManager.h"

#include <QJsonArray>
#include <QPointer>

using namespace Qt::StringLiterals;

namespace mikserstudio::services {

namespace {

using core::CommandErrorCode;
using core::CommandOutcome;
using core::CommandResult;
using core::CommandSpec;

QJsonObject objectSchema(const QJsonObject& properties, const QStringList& required)
{
    return QJsonObject{
        {u"type"_s, u"object"_s},
        {u"properties"_s, properties},
        {u"required"_s, QJsonArray::fromStringList(required)},
        {u"additionalProperties"_s, false},
    };
}

QJsonObject stringParam(const QString& description)
{
    return QJsonObject{{u"type"_s, u"string"_s}, {u"minLength"_s, 1}, {u"description"_s, description}};
}

CommandSpec aiSpec(QString id, QString title, QString description, QJsonObject schema = {})
{
    return CommandSpec{
        .id = std::move(id),
        .title = std::move(title),
        .description = std::move(description),
        .category = u"AI"_s,
        .paramsSchema = std::move(schema),
        .shortcut = {},
        .undoable = false,
    };
}

CommandSpec readOnly(CommandSpec spec)
{
    spec.readOnly = true;
    return spec;
}

CommandResult ok(QJsonObject result)
{
    return CommandOutcome{std::move(result), nullptr};
}

// Owns the job tracker and the registered commands; a child of the AiClient.
class AiCommandSet final : public QObject {
public:
    AiCommandSet(core::CommandBus& bus, core::ProjectModel& project, SidecarManager& sidecar, AiClient& client)
        : QObject(&client)
        , m_bus(&bus)
        , m_project(&project)
        , m_sidecar(&sidecar)
        , m_tracker(sidecar, client)
    {
        // project.new and project.open reuse the model: a follow-up must not reach the next project.
        connect(&project, &core::ProjectModel::projectReset, this, [this] { ++m_projectSession; });
    }

    ~AiCommandSet() override
    {
        if (m_bus) {
            for (const QString& id : std::as_const(m_ids))
                m_bus->unregisterCommand(id);
        }
    }

    void registerAll();

private:
    void add(CommandSpec spec, core::CommandHandler handler)
    {
        const QString id = spec.id;
        if (m_bus->registerCommand(std::move(spec), std::move(handler)))
            m_ids.append(id);
    }

    CommandResult status() const;
    CommandResult backends();
    CommandResult upscaleClip(const QJsonObject& params);
    CommandResult generateText(const QJsonObject& params);
    CommandResult cancelJob(const QJsonObject& params);

    QPointer<core::CommandBus> m_bus;
    QPointer<core::ProjectModel> m_project;
    QPointer<SidecarManager> m_sidecar;
    AiJobTracker m_tracker;
    QStringList m_ids;
    quint64 m_projectSession = 0;  // Bumped when the project is replaced.
};

void AiCommandSet::registerAll()
{
    add(readOnly(aiSpec(u"ai.status"_s, u"AI Service Status"_s,
                        u"Reports whether the AI service is running, its last error and the number of active jobs."_s)),
        [this](const QJsonObject&) { return status(); });
    add(aiSpec(u"ai.backends"_s, u"List AI Backends"_s,
               u"Lists the AI backends with their operations and whether they can run now."_s),
        [this](const QJsonObject&) { return backends(); });
    add(readOnly(aiSpec(u"ai.jobs"_s, u"List AI Jobs"_s,
                        u"Lists the AI jobs of this session with their progress and results."_s)),
        [this](const QJsonObject&) { return ok(QJsonObject{{u"jobs"_s, m_tracker.jobsJson()}}); });
    add(aiSpec(u"ai.cancel_job"_s, u"Cancel AI Job"_s, u"Stops a queued or running AI job."_s,
               objectSchema(QJsonObject{{u"jobId"_s, stringParam(u"Id of the job, from ai.jobs."_s)}}, {u"jobId"_s})),
        [this](const QJsonObject& params) { return cancelJob(params); });
    add(aiSpec(u"ai.upscale_clip"_s, u"Upscale Clip"_s,
               u"Upscales a video clip and places the result on the video track above it (a new one on top if needed)."_s,
               objectSchema(
                   QJsonObject{
                       {u"clipId"_s, stringParam(u"Id of the video clip to upscale."_s)},
                       {u"scale"_s, QJsonObject{{u"type"_s, u"integer"_s},
                                                {u"enum"_s, QJsonArray{2, 3, 4}},
                                                {u"default"_s, 2},
                                                {u"description"_s, u"Scale factor."_s}}},
                   },
                   {u"clipId"_s})),
        [this](const QJsonObject& params) { return upscaleClip(params); });
    add(aiSpec(u"ai.generate_text"_s, u"Generate Text"_s, u"Answers a prompt with a local language model."_s,
               objectSchema(
                   QJsonObject{
                       {u"prompt"_s, stringParam(u"What to ask the model."_s)},
                       {u"model"_s, stringParam(u"Model name; the first installed model when omitted."_s)},
                   },
                   {u"prompt"_s})),
        [this](const QJsonObject& params) { return generateText(params); });
}

CommandResult AiCommandSet::status() const
{
    if (!m_sidecar)
        return core::commandFailure(CommandErrorCode::NotAvailable, u"The AI service is not available in this session."_s);
    return ok(QJsonObject{
        {u"state"_s, toString(m_sidecar->state())},
        {u"error"_s, m_sidecar->errorString()},
        {u"baseUrl"_s, m_sidecar->baseUrl().toString()},
        {u"restarts"_s, m_sidecar->restartCount()},
        {u"activeJobs"_s, m_tracker.activeJobCount()},
    });
}

CommandResult AiCommandSet::backends()
{
    if (!m_sidecar)
        return core::commandFailure(CommandErrorCode::NotAvailable, u"The AI service is not available in this session."_s);
    if (m_sidecar->state() == SidecarState::Failed) {
        const QString error = m_sidecar->errorString();
        m_tracker.ensureSidecar();
        return core::commandFailure(CommandErrorCode::NotAvailable, error + u" Mikser Studio is trying to start it again."_s);
    }
    m_tracker.ensureSidecar();
    m_tracker.refreshBackends();
    if (!m_tracker.backends()) {
        return core::commandFailure(CommandErrorCode::NotAvailable,
                                    u"The AI service is starting. Try again in a moment."_s);
    }
    QJsonArray list;
    for (const AiBackendInfo& backend : *m_tracker.backends())
        list.append(toJson(backend));
    return ok(QJsonObject{{u"backends"_s, list}});
}

CommandResult AiCommandSet::upscaleClip(const QJsonObject& params)
{
    if (!m_project || !m_bus)
        return core::commandFailure(CommandErrorCode::NotAvailable, u"There is no open project."_s);
    const QString clipId = params.value(u"clipId").toString();
    const int scale = params.value(u"scale").toInt(2);
    auto source = resolveUpscaleSource(*m_project, clipId);
    if (!source)
        return core::commandFailure(CommandErrorCode::Failed, source.error());

    const QString jobId = m_tracker.submit(
        u"video.upscale"_s, QJsonObject{{u"input"_s, source->mediaPath}, {u"scale"_s, scale}},
        u"Upscale \"%1\" x%2"_s.arg(source->mediaName).arg(scale),
        // The tracker that runs this belongs to `this`.
        [this, session = m_projectSession, clipId, scale](const AiJobInfo& job) -> std::expected<QJsonObject, QString> {
            if (!m_bus || !m_project)
                return std::unexpected(u"The project was closed before the upscale finished."_s);
            if (session != m_projectSession) {
                return std::unexpected(
                    u"The project was replaced before the upscale finished, so the result was not added to it. Open the original project and run the upscale again."_s);
            }
            return placeUpscaledVideo(*m_bus, *m_project, clipId, scale, job);
        });
    return ok(QJsonObject{{u"jobId"_s, jobId}});
}

CommandResult AiCommandSet::generateText(const QJsonObject& params)
{
    QJsonObject request{{u"prompt"_s, params.value(u"prompt")}};
    if (params.contains(u"model"))
        request.insert(u"model"_s, params.value(u"model"));
    QString title = params.value(u"prompt").toString().simplified();
    if (title.size() > 40)
        title = title.left(39) + u"…"_s;
    const QString jobId = m_tracker.submit(u"text.generate"_s, request, u"Generate text: %1"_s.arg(title));
    return ok(QJsonObject{{u"jobId"_s, jobId}});
}

CommandResult AiCommandSet::cancelJob(const QJsonObject& params)
{
    const QString jobId = params.value(u"jobId").toString();
    auto state = m_tracker.cancel(jobId);
    if (!state)
        return std::unexpected(state.error());
    return ok(QJsonObject{{u"jobId"_s, jobId}, {u"state"_s, toString(*state)}});
}

}  // namespace

void registerAiCommands(core::CommandBus& bus, core::ProjectModel& project, SidecarManager& sidecar, AiClient& client)
{
    auto* commands = new AiCommandSet(bus, project, sidecar, client);
    commands->registerAll();
}

}  // namespace mikserstudio::services
