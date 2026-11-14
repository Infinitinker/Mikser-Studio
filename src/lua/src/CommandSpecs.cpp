// Checking the table a Lua plugin gives ms.command and turning it into a CommandSpec.
#include "CommandSpecs.h"

#include "mikserstudio/core/JsonSchema.h"

#include <QJsonDocument>

using namespace Qt::StringLiterals;

namespace mikserstudio::lua {

namespace {

const QJsonObject& specSchema()
{
    static const QJsonObject schema = QJsonDocument::fromJson(R"json({
        "type": "object",
        "required": ["id", "title"],
        "additionalProperties": false,
        "properties": {
            "id": {"type": "string"},
            "title": {"type": "string", "minLength": 1},
            "description": {"type": "string"},
            "category": {"type": "string"},
            "params": {"type": "object"},
            "result": {"type": "object"},
            "permission": {"type": "string"},
            "read_only": {"type": "boolean"},
            "confirmation": {"type": "string"},
            "open_world": {"type": "boolean"},
            "quiet": {"type": "boolean"},
            "shortcut": {"type": "string"},
            "starts_job": {"type": "boolean"}
        }
    })json")
                                          .object();
    return schema;
}

}  // namespace

std::expected<core::CommandSpec, QString> commandSpecFromJson(const QJsonObject& json, const QString& pluginId,
                                                              const QString& defaultCategory)
{
    if (const QStringList problems = core::JsonSchema::validate(json, specSchema()); !problems.isEmpty())
        return std::unexpected(u"The command's spec doesn't fit ms.command: %1"_s.arg(problems.join(u"; "_s)));
    const QString id = json.value(u"id").toString();
    if (!id.startsWith(pluginId + u'.'))
        return std::unexpected(
            u"The command id “%1” must start with the plugin's id and a dot (“%2.”)"_s.arg(id, pluginId));
    const QString permission = json.value(u"permission").toString();
    if (!permission.isEmpty() && !core::allPermissions().contains(permission))
        return std::unexpected(
            u"“%1” is not a permission; use one of %2"_s.arg(permission, core::allPermissions().join(u", "_s)));

    core::CommandSpec spec;
    spec.id = id;
    spec.title = json.value(u"title").toString();
    spec.description = json.value(u"description").toString();
    spec.category = json.value(u"category").toString(defaultCategory);
    spec.paramsSchema = json.value(u"params").toObject();
    spec.shortcut = json.value(u"shortcut").toString();
    spec.readOnly = json.value(u"read_only").toBool();
    spec.confirmation = json.value(u"confirmation").toString();
    spec.owner = pluginId;
    spec.resultSchema = json.value(u"result").toObject();
    spec.permission = permission;
    spec.startsJob = json.value(u"starts_job").toBool();
    spec.openWorld = json.value(u"open_world").toBool();
    spec.quiet = json.value(u"quiet").toBool();
    return spec;
}

}  // namespace mikserstudio::lua
