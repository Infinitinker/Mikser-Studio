// A form generated from a JSON Schema, used for command params and plugin settings.
#pragma once

#include <QJsonObject>
#include <QStringList>
#include <QWidget>

#include <memory>

namespace mikserstudio::ui {

// A form generated from a JSON Schema object (the subset in core/JsonSchema.h):
//   string -> line edit; any "enum" -> combo box; integer/number -> spin box with the schema's
//   bounds; boolean -> check box; nested objects -> group boxes; anything else (arrays, mixed
//   types) -> a line edit taking JSON.
// "title" labels a field (the property name otherwise), "description" becomes its tooltip and
// "default" its initial value. Optional fields without a default start "not set" and are left out
// of value(). validate() checks value() with core::JsonSchema and shows each problem under its
// field.
class SchemaForm final : public QWidget {
    Q_OBJECT

public:
    explicit SchemaForm(QWidget* parent = nullptr);
    explicit SchemaForm(const QJsonObject& schema, const QJsonObject& value = {}, QWidget* parent = nullptr);
    ~SchemaForm() override;

    // Rebuilds the form. `value` overrides the defaults for the properties it has.
    void setSchema(const QJsonObject& schema, const QJsonObject& value = {});
    [[nodiscard]] QJsonObject schema() const;

    [[nodiscard]] QJsonObject value() const;
    void setValue(const QJsonObject& value);  // Properties missing from `value` go back to their defaults.

    // Validates value() against the schema, shows the problems inline and returns them (empty
    // when valid).
    QStringList validate();
    void clearErrors();

    [[nodiscard]] bool hasFields() const;
    // The editor of the property at a JSON Pointer ("/name", "/options/scale"), for focus and tests.
    [[nodiscard]] QWidget* editor(const QString& pointer) const;
    // The inline error shown for a JSON Pointer (empty when there is none).
    [[nodiscard]] QString errorText(const QString& pointer) const;

Q_SIGNALS:
    void valueChanged();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

}  // namespace mikserstudio::ui
