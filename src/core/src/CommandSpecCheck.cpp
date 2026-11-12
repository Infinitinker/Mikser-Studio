// Checking command specs and dotted ids before a command or plugin is registered.
#include "CommandSpecCheck.h"

#include <QJsonArray>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

bool isValidDottedId(const QString& id)
{
    static const QRegularExpression pattern(uR"(^[a-z0-9_]+(\.[a-z0-9_]+)+$)"_s);
    return pattern.match(id).hasMatch();
}

namespace {

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
    if (spec.title.trimmed().isEmpty())
        return std::unexpected(u"command “%1” needs a title"_s.arg(spec.id));
    if (spec.readOnly && spec.undoable)
        return std::unexpected(u"command “%1” cannot be both read-only and undoable"_s.arg(spec.id));
    if (spec.paramsSchema.isEmpty()) {
        spec.paramsSchema = QJsonObject{
            {u"type"_s, u"object"_s},
            {u"properties"_s, QJsonObject()},
            {u"additionalProperties"_s, false},
        };
    }
    if (auto checked = checkParamsSchema(spec.paramsSchema); !checked)
        return std::unexpected(u"command “%1”: %2"_s.arg(spec.id, checked.error()));
    return spec;
}

}  // namespace mikserstudio::core
