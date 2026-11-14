// Checking command specs and dotted ids before a command or plugin is registered.
#include "CommandSpecCheck.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

bool isValidDottedId(const QString& id)
{
    static const QRegularExpression pattern(uR"(^[a-z0-9_]+(\.[a-z0-9_]+)+$)"_s);
    return pattern.match(id).hasMatch();
}

QString toolNameOf(const QString& commandId)
{
    return QString(commandId).replace(u'.', u'_');
}

namespace {

constexpr qsizetype kMaxToolNameLength = 64;  // MCP's limit for a tool name.
const QString kReservedToolName = u"recent_events"_s;  // The MCP bridge's own tool.

std::expected<void, QString> checkConfirmationParam(const CommandSpec& spec)
{
    if (spec.confirmationParam.isEmpty())
        return {};
    const QJsonValue property = spec.paramsSchema.value(u"properties").toObject().value(spec.confirmationParam);
    if (!property.isObject())
        return std::unexpected(
            u"confirmationParam “%1” must be a property of paramsSchema"_s.arg(spec.confirmationParam));
    const QJsonObject schema = property.toObject();
    if (schema.value(u"type") != u"boolean"_s || schema.value(u"default") != QJsonValue(false))
        return std::unexpected(
            u"confirmationParam “%1” must be a boolean property with \"default\": false"_s.arg(spec.confirmationParam));
    return {};
}

std::expected<void, QString> checkParamsSchema(const QJsonObject& schema)
{
    if (schema.value(u"type") != u"object"_s)
        return std::unexpected(u"paramsSchema must have \"type\": \"object\" because params are a JSON object"_s);
    if (schema.contains(u"properties")) {
        const QJsonValue properties = schema.value(u"properties");
        if (!properties.isObject())
            return std::unexpected(u"paramsSchema “properties” must be an object"_s);
        const QJsonObject propertyMap = properties.toObject();
        for (auto it = propertyMap.begin(); it != propertyMap.end(); ++it) {
            if (!it.value().isObject())
                return std::unexpected(u"paramsSchema property “%1” must be a schema object"_s.arg(it.key()));
        }
    }
    if (schema.contains(u"required")) {
        const QJsonValue required = schema.value(u"required");
        if (!required.isArray())
            return std::unexpected(u"paramsSchema “required” must be an array of property names"_s);
        for (const QJsonValue name : required.toArray()) {
            if (!name.isString())
                return std::unexpected(u"paramsSchema “required” must contain only strings"_s);
        }
    }
    return {};
}

}  // namespace

std::expected<CommandSpec, QString> normalizeCommandSpec(CommandSpec spec)
{
    if (!isValidDottedId(spec.id)) {
        return std::unexpected(
            u"command id “%1” must be dotted lower_snake_case with at least two parts, e.g. \"timeline.add_clip\""_s
                .arg(spec.id));
    }
    const QString tool = toolNameOf(spec.id);
    if (tool.size() > kMaxToolNameLength) {
        return std::unexpected(u"command id “%1” is too long: with “_” for “.” it must have at most %2 characters, "
                               u"the length of an MCP tool name"_s.arg(spec.id)
                                   .arg(kMaxToolNameLength));
    }
    if (tool == kReservedToolName) {
        return std::unexpected(u"command id “%1” is reserved: the MCP bridge offers a tool of its own named “%2”"_s.arg(
            spec.id, kReservedToolName));
    }
    if (spec.title.trimmed().isEmpty())
        return std::unexpected(u"command “%1” needs a title"_s.arg(spec.id));
    if (spec.readOnly && spec.undoable)
        return std::unexpected(u"command “%1” cannot be both read-only and undoable"_s.arg(spec.id));
    if (spec.readOnly && (!spec.confirmation.isEmpty() || !spec.confirmationParam.isEmpty()))
        return std::unexpected(
            u"command “%1” is read-only, so it changes nothing and must not ask first (confirmation or confirmationParam)"_s
                .arg(spec.id));
    if (spec.paramsSchema.isEmpty()) {
        spec.paramsSchema = QJsonObject{
            {u"type"_s, u"object"_s},
            {u"properties"_s, QJsonObject()},
            {u"additionalProperties"_s, false},
        };
    }
    if (auto checked = checkParamsSchema(spec.paramsSchema); !checked)
        return std::unexpected(u"command “%1”: %2"_s.arg(spec.id, checked.error()));
    if (auto checked = checkConfirmationParam(spec); !checked)
        return std::unexpected(u"command “%1”: %2"_s.arg(spec.id, checked.error()));
    if (!spec.resultSchema.isEmpty() && spec.resultSchema.value(u"type") != u"object"_s)
        return std::unexpected(
            u"command “%1”: resultSchema must have \"type\": \"object\" because results are a JSON object"_s.arg(
                spec.id));
    if (spec.owner.isEmpty())
        return std::unexpected(u"command “%1” needs an owner: \"core\" or the plugin's id"_s.arg(spec.id));
    if (spec.permission.isEmpty())
        spec.permission = spec.readOnly ? u"project.read"_s : u"project.write"_s;
    if (!allPermissions().contains(spec.permission))
        return std::unexpected(u"command “%1” names the unknown permission “%2”; use one of %3"_s.arg(
            spec.id, spec.permission, allPermissions().join(u", "_s)));
    return spec;
}

}  // namespace mikserstudio::core
