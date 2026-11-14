// Registering the ai.* commands on the AI engine and bringing finished jobs into the project.
#include "mikserstudio/services/AiCommands.h"

#include "AiResultImport.h"
#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/services/AiEngine.h"

#include <QJsonArray>
#include <QPointer>

using namespace Qt::StringLiterals;

namespace mikserstudio::services {

namespace {

using core::CommandErrorCode;
using core::CommandOutcome;
using core::CommandResult;
using core::CommandSpec;

constexpr qsizetype kTitleChars = 40;

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

// A command that changes where the AI tools are reached.
CommandSpec network(CommandSpec spec)
{
    spec.permission = u"network"_s;
    return spec;
}

// A command that talks to the AI tools, which may be on another computer or an online service.
CommandSpec reachesAiTools(CommandSpec spec)
{
    spec.openWorld = true;
    return network(std::move(spec));
}

// A command that starts an AI job and answers with its {jobId}.
CommandSpec startsJob(CommandSpec spec)
{
    spec.startsJob = true;
    spec.resultSchema = core::objectResultSchema({{u"jobId"_s, u"string"_s}});
    return reachesAiTools(std::move(spec));
}

CommandResult ok(QJsonObject result)
{
    return CommandOutcome{std::move(result), nullptr};
}

QJsonArray backendsJson(const QList<AiBackendInfo>& backends)
{
    QJsonArray list;
    for (const AiBackendInfo& backend : backends) {
        QJsonArray ops;
        for (const AiOperation& operation : backend.operations) {
            ops.append(QJsonObject{
                {u"id"_s, operation.id},
                {u"title"_s, operation.title},
                {u"description"_s, operation.description},
                {u"paramsSchema"_s, operation.paramsSchema},
            });
        }
        list.append(QJsonObject{
            {u"id"_s, backend.id},
            {u"title"_s, backend.title},
            {u"ops"_s, ops},
            {u"available"_s, backend.available},
            {u"reason"_s, backend.reason},
        });
    }
    return list;
}

// The prompt, on one line and short, for job titles and media names.
QString shortPrompt(const QJsonObject& params)
{
    QString text = params.value(u"prompt").toString().simplified();
    if (text.size() > kTitleChars)
        text = text.left(kTitleChars - 1) + u"…"_s;
    return text;
}

// Owns the registered commands; a child of the engine.
class AiCommandSet final : public QObject {
public:
    AiCommandSet(core::CommandBus& bus, core::ProjectModel& project, AiEngine& engine)
        : QObject(&engine)
        , m_bus(&bus)
        , m_project(&project)
        , m_engine(engine)
    {
        // project.new and project.open reuse the model: a result must not reach the next project.
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

    CommandResult run(const QJsonObject& params);
    CommandResult cancelJob(const QJsonObject& params);
    CommandResult setSettings(const QJsonObject& params);
    std::expected<QJsonObject, QString> bringIntoProject(const QJsonObject& job, quint64 session, const QString& name);

    QPointer<core::CommandBus> m_bus;
    QPointer<core::ProjectModel> m_project;
    AiEngine& m_engine;
    QStringList m_ids;
    quint64 m_projectSession = 0;  // Bumped when the project is replaced.
};

void AiCommandSet::registerAll()
{
    add(startsJob(aiSpec(
            u"ai.run"_s, u"Run AI operation"_s,
            u"Runs an AI operation, such as image.generate or text.generate, in the AI tool of its backend; images, video and audio it makes come into the project."_s,
            objectSchema(
                QJsonObject{
                    {u"op"_s, QJsonObject{{u"type"_s, u"string"_s},
                                          {u"enum"_s, QJsonArray::fromStringList(m_engine.operationIds())},
                                          {u"description"_s, u"The operation, from ai.backends."_s}}},
                    {u"params"_s, QJsonObject{{u"type"_s, u"object"_s},
                                              {u"default"_s, QJsonObject{}},
                                              {u"description"_s,
                                               u"The operation's parameters, as its schema in ai.backends says."_s}}},
                    {u"backend"_s, stringParam(u"The backend to run it on; the first that offers it when omitted."_s)},
                },
                {u"op"_s}))),
        [this](const QJsonObject& params) { return run(params); });
    add(readOnly(aiSpec(u"ai.jobs"_s, u"List AI jobs"_s,
                        u"Lists the AI jobs of this session with their progress and results."_s)),
        [this](const QJsonObject&) { return ok(QJsonObject{{u"jobs"_s, m_engine.jobsJson()}}); });
    add(reachesAiTools(aiSpec(
            u"ai.cancel_job"_s, u"Cancel AI job"_s, u"Stops a queued or running AI job."_s,
            objectSchema(QJsonObject{{u"jobId"_s, stringParam(u"Id of the job, from ai.jobs."_s)}}, {u"jobId"_s}))),
        [this](const QJsonObject& params) { return cancelJob(params); });
    add(reachesAiTools(
            aiSpec(u"ai.backends"_s, u"List AI backends"_s,
                   u"Lists the AI backends with their operations and whether their AI tools can run them now."_s)),
        [this](const QJsonObject&) {
            m_engine.refreshAvailability();
            return ok(QJsonObject{{u"backends"_s, backendsJson(m_engine.backends())}});
        });
    add(readOnly(aiSpec(u"ai.status"_s, u"AI status"_s,
                        u"Reports the number of active AI jobs and the addresses of the AI tools."_s)),
        [this](const QJsonObject&) {
            QJsonObject status = toJson(m_engine.settings());
            status.insert(u"activeJobs"_s, m_engine.activeJobCount());
            return ok(status);
        });
    add(readOnly(aiSpec(u"ai.settings"_s, u"AI settings"_s, u"Reports the addresses of the AI tools."_s)),
        [this](const QJsonObject&) { return ok(toJson(m_engine.settings())); });
    add(network(aiSpec(u"ai.set_settings"_s, u"Change AI settings"_s,
                       u"Changes the addresses of the AI tools; new jobs use them at once, running jobs go on."_s,
                       objectSchema(
                           QJsonObject{
                               {u"ollamaUrl"_s, stringParam(u"Address of Ollama, such as http://127.0.0.1:11434."_s)},
                               {u"comfyuiUrl"_s, stringParam(u"Address of ComfyUI, such as http://127.0.0.1:8188."_s)},
                           },
                           {}))),
        [this](const QJsonObject& params) { return setSettings(params); });
}

CommandResult AiCommandSet::run(const QJsonObject& params)
{
    const QString op = params.value(u"op").toString();
    const QJsonObject opParams = params.value(u"params").toObject();
    const QString prompt = shortPrompt(opParams);
    const QString name = prompt.isEmpty() ? u"AI result"_s : prompt;
    auto submitted = m_engine.submit(op, opParams, params.value(u"backend").toString(), prompt,
                                     // The engine that runs this owns `this`.
                                     [this, session = m_projectSession, name](const QJsonObject& job) {
                                         return bringIntoProject(job, session, name);
                                     });
    if (!submitted)
        return std::unexpected(submitted.error());
    return ok(QJsonObject{{u"jobId"_s, *submitted}});
}

std::expected<QJsonObject, QString> AiCommandSet::bringIntoProject(const QJsonObject& job, quint64 session,
                                                                   const QString& name)
{
    if (!m_bus || !m_project)
        return std::unexpected(u"The project was closed before the AI job finished."_s);
    if (session != m_projectSession) {
        return std::unexpected(
            u"The project was replaced before the AI job finished, so the result was not added to it. Its files are in Mikser Studio’s cache; open the original project and run the job again."_s);
    }
    return importAiOutputs(*m_bus, job, name);
}

CommandResult AiCommandSet::cancelJob(const QJsonObject& params)
{
    const QString jobId = params.value(u"jobId").toString();
    auto state = m_engine.cancel(jobId);
    if (!state)
        return std::unexpected(state.error());
    return ok(QJsonObject{{u"jobId"_s, jobId}, {u"state"_s, toString(*state)}});
}

CommandResult AiCommandSet::setSettings(const QJsonObject& params)
{
    auto settings = aiSettingsFromJson(params, m_engine.settings());
    if (!settings)
        return core::commandFailure(CommandErrorCode::InvalidParams, settings.error());
    if (auto saved = m_engine.setSettings(*settings); !saved)
        return core::commandFailure(CommandErrorCode::Failed, saved.error());
    return ok(toJson(m_engine.settings()));
}

}  // namespace

void registerAiCommands(core::CommandBus& bus, core::ProjectModel& project, AiEngine& engine)
{
    auto* commands = new AiCommandSet(bus, project, engine);
    commands->registerAll();
}

}  // namespace mikserstudio::services
