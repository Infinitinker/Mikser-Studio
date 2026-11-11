#include "mikserstudio/ui/SchemaForm.h"

#include <catch2/catch_test_macros.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QSignalSpy>
#include <QSpinBox>

using namespace Qt::StringLiterals;
using mikserstudio::ui::SchemaForm;

namespace {

QJsonObject parseObject(const char* json)
{
    return QJsonDocument::fromJson(QByteArray(json)).object();
}

QJsonObject clipSchema()
{
    return parseObject(R"({
        "type": "object",
        "required": ["name", "count"],
        "properties": {
            "name": {"type": "string", "title": "Clip name", "description": "Shown on the timeline.", "minLength": 1},
            "count": {"type": "integer", "minimum": 1, "maximum": 10, "default": 2},
            "mode": {"type": "string", "enum": ["fast", "slow"], "default": "fast"},
            "gain": {"type": "number", "minimum": 0, "maximum": 4},
            "loud": {"type": "boolean"},
            "tags": {"type": "array", "items": {"type": "string"}},
            "options": {"type": "object", "properties": {"scale": {"type": "integer", "default": 3}}}
        }
    })");
}

QString compact(const QJsonObject& object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

}  // namespace

TEST_CASE("SchemaForm creates an editor per property type", "[schema-form]")
{
    SchemaForm form(clipSchema());
    REQUIRE(form.hasFields());

    auto* name = qobject_cast<QLineEdit*>(form.editor(u"/name"_s));
    REQUIRE(name);
    CHECK(name->toolTip().contains(u"Shown on the timeline."_s));

    auto* count = qobject_cast<QSpinBox*>(form.editor(u"/count"_s));
    REQUIRE(count);
    CHECK(count->minimum() == 1);
    CHECK(count->maximum() == 10);
    CHECK(count->value() == 2);

    auto* mode = qobject_cast<QComboBox*>(form.editor(u"/mode"_s));
    REQUIRE(mode);
    CHECK(mode->currentText() == u"fast"_s);

    CHECK(qobject_cast<QDoubleSpinBox*>(form.editor(u"/gain"_s)));
    CHECK(qobject_cast<QCheckBox*>(form.editor(u"/loud"_s)));
    CHECK(qobject_cast<QLineEdit*>(form.editor(u"/tags"_s)));  // JSON fallback.
    CHECK(qobject_cast<QSpinBox*>(form.editor(u"/options/scale"_s)));
    CHECK(form.editor(u"/missing"_s) == nullptr);
}

TEST_CASE("SchemaForm starts from the schema defaults and leaves unset optional fields out", "[schema-form]")
{
    SchemaForm form(clipSchema());
    const QJsonObject value = form.value();
    INFO(compact(value).toStdString());
    CHECK(value.value(u"name"_s) == u""_s);  // Required: always present.
    CHECK(value.value(u"count"_s) == 2);
    CHECK(value.value(u"mode"_s) == u"fast"_s);
    CHECK(value.value(u"options"_s).toObject().value(u"scale"_s) == 3);
    CHECK_FALSE(value.contains(u"gain"_s));
    CHECK_FALSE(value.contains(u"loud"_s));
    CHECK_FALSE(value.contains(u"tags"_s));
}

TEST_CASE("SchemaForm takes initial values over defaults and setValue resets the rest", "[schema-form]")
{
    SchemaForm form(clipSchema(), parseObject(R"({"name": "Intro", "mode": "slow", "tags": ["a", "b"]})"));
    QJsonObject value = form.value();
    CHECK(value.value(u"name"_s) == u"Intro"_s);
    CHECK(value.value(u"mode"_s) == u"slow"_s);
    CHECK(value.value(u"tags"_s).toArray() == QJsonArray{u"a"_s, u"b"_s});
    CHECK(value.value(u"count"_s) == 2);

    QSignalSpy changed(&form, &SchemaForm::valueChanged);
    form.setValue(parseObject(R"({"name": "Outro", "count": 7, "loud": true, "gain": 1.5})"));
    CHECK(changed.count() >= 1);
    value = form.value();
    INFO(compact(value).toStdString());
    CHECK(value.value(u"name"_s) == u"Outro"_s);
    CHECK(value.value(u"count"_s) == 7);
    CHECK(value.value(u"loud"_s) == true);
    CHECK(value.value(u"gain"_s).toDouble() == 1.5);
    CHECK(value.value(u"mode"_s) == u"fast"_s);  // Back to the default.
    CHECK_FALSE(value.contains(u"tags"_s));
}

TEST_CASE("SchemaForm validates and shows each problem under its field", "[schema-form]")
{
    SchemaForm form(clipSchema());

    QStringList problems = form.validate();
    REQUIRE(problems.size() == 1);
    CHECK(problems.first().startsWith(u"/name: "_s));
    CHECK_FALSE(form.errorText(u"/name"_s).isEmpty());
    CHECK(form.errorText(u"/count"_s).isEmpty());

    qobject_cast<QLineEdit*>(form.editor(u"/name"_s))->setText(u"Intro"_s);
    CHECK(form.validate().isEmpty());
    CHECK(form.errorText(u"/name"_s).isEmpty());

    SECTION("malformed JSON in a fallback editor is reported before schema validation")
    {
        qobject_cast<QLineEdit*>(form.editor(u"/tags"_s))->setText(u"[1,"_s);
        problems = form.validate();
        REQUIRE(problems.size() == 1);
        CHECK(problems.first().startsWith(u"/tags: "_s));
        CHECK(form.errorText(u"/tags"_s).contains(u"valid JSON"_s));
    }

    SECTION("schema problems inside a JSON value keep their pointer")
    {
        qobject_cast<QLineEdit*>(form.editor(u"/tags"_s))->setText(u"[\"a\", 2]"_s);
        problems = form.validate();
        REQUIRE(problems.size() == 1);
        CHECK(problems.first().startsWith(u"/tags/1: "_s));
        CHECK(form.errorText(u"/tags"_s).startsWith(u"/tags/1: "_s));
    }

    SECTION("clearErrors hides the messages")
    {
        qobject_cast<QLineEdit*>(form.editor(u"/name"_s))->clear();
        REQUIRE_FALSE(form.validate().isEmpty());
        form.clearErrors();
        CHECK(form.errorText(u"/name"_s).isEmpty());
    }
}

TEST_CASE("SchemaForm emits valueChanged on edits and handles schemas without fields", "[schema-form]")
{
    SchemaForm form(clipSchema());
    QSignalSpy changed(&form, &SchemaForm::valueChanged);
    qobject_cast<QCheckBox*>(form.editor(u"/loud"_s))->setChecked(true);
    CHECK(changed.count() == 1);
    CHECK(form.value().value(u"loud"_s) == true);

    form.setSchema(parseObject(R"({"type": "object", "properties": {}})"));
    CHECK_FALSE(form.hasFields());
    CHECK(form.value().isEmpty());
    CHECK(form.validate().isEmpty());
}
