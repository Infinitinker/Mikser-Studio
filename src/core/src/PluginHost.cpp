#include "PluginHost.h"

#include "Logging.h"
#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/JsonSchema.h"

#include <QJsonDocument>

#include <utility>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

namespace {

const QJsonObject& commandSpecSchema()
{
    static const QJsonObject schema = QJsonDocument::fromJson(R"json({
        "type": "object",
        "required": ["id", "title"],
        "properties": {
            "id": {"type": "string"},
            "title": {"type": "string", "minLength": 1},
            "description": {"type": "string"},
            "category": {"type": "string"},
            "paramsSchema": {"type": "object"},
            "shortcut": {"type": "string"},
            "readOnly": {"type": "boolean"},
            "confirmation": {"type": "string"}
        }
    })json").object();
    return schema;
}

QByteArray toUtf8(const char* data, size_t length)
{
    return data ? QByteArray(data, static_cast<qsizetype>(length)) : QByteArray();
}

// Parses a JSON object sent over the ABI; empty input means {}.
std::expected<QJsonObject, QString> parseObject(const QByteArray& json)
{
    if (json.trimmed().isEmpty())
        return QJsonObject();
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError)
        return std::unexpected(u"invalid JSON (%1 at offset %2)"_s.arg(error.errorString()).arg(error.offset));
    if (!document.isObject())
        return std::unexpected(u"expected a JSON object"_s);
    return document.object();
}

QByteArray compact(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

}  // namespace

void SinkBuffer::write(void* context, const char* utf8, size_t length)
{
    static_cast<SinkBuffer*>(context)->m_data.append(toUtf8(utf8, length));
}

void writeToSink(const MikserStudioStringSink* sink, const QByteArray& data)
{
    if (sink && sink->write)
        sink->write(sink->context, data.constData(), static_cast<size_t>(data.size()));
}

PluginHost::PluginHost(CommandBus& bus, QString pluginId, QString category, SettingsProvider settings)
    : m_bus(bus)
    , m_pluginId(std::move(pluginId))
    , m_category(std::move(category))
    , m_settings(std::move(settings))
{
    m_api.struct_size = sizeof(MikserStudioHostApiV1);
    m_api.api_version = MIKSERSTUDIO_PLUGIN_API_VERSION;
    m_api.host = this;
    m_api.log = &PluginHost::log;
    m_api.register_command = &PluginHost::registerCommand;
    m_api.execute_command = &PluginHost::executeCommand;
    m_api.get_settings = &PluginHost::getSettings;
}

PluginHost::~PluginHost()
{
    unregisterCommands();
}

void PluginHost::unregisterCommands()
{
    const QStringList commands = std::exchange(m_commands, {});
    for (const QString& id : commands)
        m_bus.unregisterCommand(id);
}

PluginHost& PluginHost::self(void* host)
{
    return *static_cast<PluginHost*>(host);
}

void PluginHost::log(void* host, MikserStudioLogLevel level, const char* message, size_t length)
{
    const QString text = u"[%1] %2"_s.arg(self(host).m_pluginId, QString::fromUtf8(toUtf8(message, length)));
    switch (level) {
    case MIKSERSTUDIO_LOG_DEBUG:
        qCDebug(lcPlugins).noquote() << text;
        break;
    case MIKSERSTUDIO_LOG_INFO:
        qCInfo(lcPlugins).noquote() << text;
        break;
    case MIKSERSTUDIO_LOG_WARNING:
        qCWarning(lcPlugins).noquote() << text;
        break;
    case MIKSERSTUDIO_LOG_ERROR:
    default:
        qCCritical(lcPlugins).noquote() << text;
        break;
    }
}

int PluginHost::registerCommand(void* host, const char* specJson, size_t specLength, MikserStudioCommandFn fn,
                                void* userData, const MikserStudioStringSink* error)
{
    PluginHost& plugin = self(host);
    auto added = plugin.addCommand(toUtf8(specJson, specLength), fn, userData);
    if (added)
        return 0;
    qCWarning(lcPlugins).noquote() << "Plugin" << plugin.m_pluginId << "could not register a command:" << added.error();
    writeToSink(error, added.error().toUtf8());
    return 1;
}

std::expected<void, QString> PluginHost::addCommand(const QByteArray& specJson, MikserStudioCommandFn fn, void* userData)
{
    if (!fn)
        return std::unexpected(u"the command function is null"_s);
    auto json = parseObject(specJson);
    if (!json)
        return std::unexpected(u"the command spec is not valid: %1"_s.arg(json.error()));
    if (const QStringList problems = JsonSchema::validate(*json, commandSpecSchema()); !problems.isEmpty())
        return std::unexpected(u"the command spec is not valid: %1"_s.arg(problems.join(u"; "_s)));

    const QString id = json->value(u"id").toString();
    if (!id.startsWith(m_pluginId + u'.'))
        return std::unexpected(u"command id \"%1\" must start with the plugin id followed by a dot (\"%2.\")"_s.arg(id, m_pluginId));

    CommandSpec spec{
        .id = id,
        .title = json->value(u"title").toString(),
        .description = json->value(u"description").toString(),
        .category = json->value(u"category").toString(m_category),
        .paramsSchema = json->value(u"paramsSchema").toObject(),
        .shortcut = json->value(u"shortcut").toString(),
        .undoable = false,
        .readOnly = json->value(u"readOnly").toBool(),
        .confirmation = json->value(u"confirmation").toString(),
    };
    CommandHandler handler = [fn, userData, id, pluginId = m_pluginId](const QJsonObject& params) -> CommandResult {
        const QByteArray input = compact(params);
        SinkBuffer result;
        SinkBuffer error;
        if (fn(userData, input.constData(), static_cast<size_t>(input.size()), result.sink(), error.sink()) != 0) {
            return commandFailure(CommandErrorCode::Failed,
                                  error.data().isEmpty() ? u"The plugin command \"%1\" failed without giving a reason."_s.arg(id)
                                                         : error.text());
        }
        auto output = parseObject(result.data());
        if (!output) {
            return commandFailure(CommandErrorCode::Failed,
                                  u"Plugin \"%1\" returned a broken result for \"%2\" (%3). Report this to the plugin author."_s
                                      .arg(pluginId, id, output.error()));
        }
        return CommandOutcome{std::move(*output), nullptr};
    };

    if (!m_bus.registerCommand(std::move(spec), std::move(handler))) {
        return std::unexpected(m_bus.contains(id) ? u"command id \"%1\" is already registered"_s.arg(id)
                                                  : u"command \"%1\" was rejected; check its id format and paramsSchema "
                                                    "(the log has details)"_s.arg(id));
    }
    m_commands.append(id);
    return {};
}

int PluginHost::executeCommand(void* host, const char* id, size_t idLength, const char* paramsJson,
                               size_t paramsLength, const MikserStudioStringSink* result, const MikserStudioStringSink* error)
{
    PluginHost& plugin = self(host);
    const QString commandId = QString::fromUtf8(toUtf8(id, idLength));
    auto params = parseObject(toUtf8(paramsJson, paramsLength));
    if (!params) {
        writeToSink(error, u"The params for \"%1\" are not valid: %2"_s.arg(commandId, params.error()).toUtf8());
        return 1;
    }
    auto outcome = plugin.m_bus.execute(commandId, *params);
    if (!outcome) {
        writeToSink(error, outcome.error().message.toUtf8());
        return 1;
    }
    writeToSink(result, compact(*outcome));
    return 0;
}

void PluginHost::getSettings(void* host, const MikserStudioStringSink* settingsJson)
{
    writeToSink(settingsJson, compact(self(host).m_settings()));
}

}  // namespace mikserstudio::core
