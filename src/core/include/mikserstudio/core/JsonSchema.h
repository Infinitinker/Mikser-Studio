#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace mikserstudio::core {

// Validates JSON values against the subset of JSON Schema (draft 2020-12) that Mikser Studio uses for
// command params, plugin settings and manifests:
//   type (string | number | integer | boolean | object | array | null, or an array of them),
//   enum, const, minimum, maximum, exclusiveMinimum, exclusiveMaximum, minLength, maxLength,
//   pattern, properties, required, additionalProperties (bool or schema), items, minItems,
//   maxItems, default (ignored for validation), title/description (ignored).
// Unknown keywords are ignored so schemas stay forward compatible.
class JsonSchema {
public:
    // Returns one English message per problem, each prefixed with a JSON Pointer to the value,
    // e.g. "/position: must be >= 0" ("~" and "/" in keys are escaped as "~0" and "~1"; problems
    // with the value itself are prefixed "(root)"). "integer" accepts numbers without a fraction,
    // "pattern" is an unanchored QRegularExpression search, lengths count Unicode code points.
    // Empty means valid.
    [[nodiscard]] static QStringList validate(const QJsonValue& value, const QJsonObject& schema);

    // Fills missing properties that have a "default" in the schema (top level and nested objects
    // that are present or come from a default). Does not validate.
    [[nodiscard]] static QJsonObject applyDefaults(const QJsonObject& value, const QJsonObject& schema);
};

}  // namespace mikserstudio::core
