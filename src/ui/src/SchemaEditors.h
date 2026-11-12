// The input widgets for the properties of a JSON Schema, which the schema form is built from.
#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

class QWidget;

namespace mikserstudio::ui {

// The input widget of one non-object schema property, used by SchemaForm.
class FieldEditor {
public:
    virtual ~FieldEditor() = default;

    [[nodiscard]] virtual QWidget* widget() const = 0;
    // std::nullopt means "not set": the property is left out of the form's value.
    [[nodiscard]] virtual std::optional<QJsonValue> value() const = 0;
    virtual void setValue(const std::optional<QJsonValue>& value) = 0;
    // A problem with the raw input that schema validation cannot see (malformed JSON).
    [[nodiscard]] virtual QString inputError() const { return {}; }
};

// Picks the editor for `schema`. `optional` editors can be "not set" (an empty line edit, a
// "Not set" spin box or combo entry, an unchecked box). `changed` runs on every edit.
[[nodiscard]] std::unique_ptr<FieldEditor> createFieldEditor(const QJsonObject& schema, bool optional,
                                                             const std::function<void()>& changed);

// The JSON Schema "type" of `schema` when it names exactly one type, else empty.
[[nodiscard]] QString singleSchemaType(const QJsonObject& schema);

}  // namespace mikserstudio::ui
