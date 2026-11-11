#include "SchemaEditors.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QSpinBox>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

namespace {

const auto kNotSet = u"Not set"_s;

std::optional<double> number(const QJsonObject& schema, QStringView key)
{
    const QJsonValue value = schema.value(key);
    return value.isDouble() ? std::optional(value.toDouble()) : std::nullopt;
}

QString display(const QJsonValue& value)
{
    if (value.isString())
        return value.toString();
    if (value.isBool())
        return value.toBool() ? u"true"_s : u"false"_s;
    if (value.isDouble())
        return QString::number(value.toDouble());
    if (value.isNull())
        return u"null"_s;
    return QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact).sliced(1).chopped(1));
}

int clampToInt(double value)
{
    constexpr double low = std::numeric_limits<int>::min() + 1.0;
    constexpr double high = std::numeric_limits<int>::max();
    return static_cast<int>(std::clamp(value, low, high));
}

class StringEditor final : public FieldEditor {
public:
    StringEditor(bool optional, const std::function<void()>& changed)
        : m_edit(new QLineEdit)
        , m_optional(optional)
    {
        if (optional)
            m_edit->setPlaceholderText(u"Optional"_s);
        QObject::connect(m_edit, &QLineEdit::textChanged, m_edit, [changed] { changed(); });
    }
    QWidget* widget() const override { return m_edit; }
    std::optional<QJsonValue> value() const override
    {
        if (m_optional && m_edit->text().isEmpty())
            return std::nullopt;
        return QJsonValue(m_edit->text());
    }
    void setValue(const std::optional<QJsonValue>& value) override
    {
        m_edit->setText(value && value->isString() ? value->toString() : QString());
    }

private:
    QLineEdit* m_edit;
    bool m_optional;
};

class EnumEditor final : public FieldEditor {
public:
    EnumEditor(const QJsonArray& options, bool optional, const std::function<void()>& changed)
        : m_combo(new QComboBox)
    {
        if (optional) {
            m_combo->addItem(kNotSet);
            m_values.append(std::nullopt);
        }
        for (const QJsonValue& option : options) {
            m_combo->addItem(display(option));
            m_values.append(option);
        }
        QObject::connect(m_combo, &QComboBox::currentIndexChanged, m_combo, [changed] { changed(); });
    }
    QWidget* widget() const override { return m_combo; }
    std::optional<QJsonValue> value() const override { return m_values.value(m_combo->currentIndex()); }
    void setValue(const std::optional<QJsonValue>& value) override
    {
        qsizetype index = 0;
        for (qsizetype i = 0; i < m_values.size(); ++i) {
            const auto& candidate = m_values.at(i);
            const bool same = candidate && value
                && (*candidate == *value
                    || (candidate->isDouble() && value->isDouble() && candidate->toDouble() == value->toDouble()));
            if (same || (!candidate && !value)) {
                index = i;
                break;
            }
        }
        m_combo->setCurrentIndex(static_cast<int>(index));
    }

private:
    QComboBox* m_combo;
    QList<std::optional<QJsonValue>> m_values;
};

class IntegerEditor final : public FieldEditor {
public:
    IntegerEditor(const QJsonObject& schema, bool optional, const std::function<void()>& changed)
        : m_spin(new QSpinBox)
        , m_optional(optional)
    {
        int low = std::numeric_limits<int>::min() + 1;
        int high = std::numeric_limits<int>::max();
        if (const auto minimum = number(schema, u"minimum"))
            low = std::max(low, clampToInt(std::ceil(*minimum)));
        if (const auto exclusive = number(schema, u"exclusiveMinimum"))
            low = std::max(low, clampToInt(std::floor(*exclusive) + 1.0));
        if (const auto maximum = number(schema, u"maximum"))
            high = std::min(high, clampToInt(std::floor(*maximum)));
        if (const auto exclusive = number(schema, u"exclusiveMaximum"))
            high = std::min(high, clampToInt(std::ceil(*exclusive) - 1.0));
        high = std::max(high, low);
        m_low = low;
        m_high = high;
        if (optional) {
            m_spin->setRange(low - 1, high);
            m_spin->setSpecialValueText(kNotSet);
        } else {
            m_spin->setRange(low, high);
        }
        m_spin->setAccelerated(true);
        QObject::connect(m_spin, &QSpinBox::valueChanged, m_spin, [changed] { changed(); });
    }
    QWidget* widget() const override { return m_spin; }
    std::optional<QJsonValue> value() const override
    {
        if (m_optional && m_spin->value() == m_spin->minimum())
            return std::nullopt;
        return QJsonValue(m_spin->value());
    }
    void setValue(const std::optional<QJsonValue>& value) override
    {
        if (value && value->isDouble())
            m_spin->setValue(std::clamp(clampToInt(value->toDouble()), m_low, m_high));
        else
            m_spin->setValue(m_optional ? m_spin->minimum() : std::clamp(0, m_low, m_high));
    }

private:
    QSpinBox* m_spin;
    bool m_optional;
    int m_low = 0;
    int m_high = 0;
};

class NumberEditor final : public FieldEditor {
public:
    NumberEditor(const QJsonObject& schema, bool optional, const std::function<void()>& changed)
        : m_spin(new QDoubleSpinBox)
        , m_optional(optional)
    {
        constexpr double kStep = 0.001;
        constexpr double kLimit = 1e12;
        double low = -kLimit;
        double high = kLimit;
        if (const auto minimum = number(schema, u"minimum"))
            low = std::max(low, *minimum);
        if (const auto exclusive = number(schema, u"exclusiveMinimum"))
            low = std::max(low, *exclusive + kStep);
        if (const auto maximum = number(schema, u"maximum"))
            high = std::min(high, *maximum);
        if (const auto exclusive = number(schema, u"exclusiveMaximum"))
            high = std::min(high, *exclusive - kStep);
        high = std::max(high, low);
        m_low = low;
        m_high = high;
        m_spin->setDecimals(3);
        m_spin->setSingleStep(high - low <= 10.0 ? 0.1 : 1.0);
        if (optional) {
            m_spin->setRange(low - 1.0, high);
            m_spin->setSpecialValueText(kNotSet);
        } else {
            m_spin->setRange(low, high);
        }
        m_spin->setAccelerated(true);
        QObject::connect(m_spin, &QDoubleSpinBox::valueChanged, m_spin, [changed] { changed(); });
    }
    QWidget* widget() const override { return m_spin; }
    std::optional<QJsonValue> value() const override
    {
        if (m_optional && m_spin->value() == m_spin->minimum())
            return std::nullopt;
        return QJsonValue(m_spin->value());
    }
    void setValue(const std::optional<QJsonValue>& value) override
    {
        if (value && value->isDouble())
            m_spin->setValue(std::clamp(value->toDouble(), m_low, m_high));
        else
            m_spin->setValue(m_optional ? m_spin->minimum() : std::clamp(0.0, m_low, m_high));
    }

private:
    QDoubleSpinBox* m_spin;
    bool m_optional;
    double m_low = 0.0;
    double m_high = 0.0;
};

class BooleanEditor final : public FieldEditor {
public:
    BooleanEditor(bool optional, const std::function<void()>& changed)
        : m_box(new QCheckBox)
        , m_optional(optional)
    {
        QObject::connect(m_box, &QCheckBox::toggled, m_box, [changed] { changed(); });
    }
    QWidget* widget() const override { return m_box; }
    std::optional<QJsonValue> value() const override
    {
        if (m_optional && !m_box->isChecked())
            return std::nullopt;
        return QJsonValue(m_box->isChecked());
    }
    void setValue(const std::optional<QJsonValue>& value) override
    {
        m_box->setChecked(value && value->toBool());
    }

private:
    QCheckBox* m_box;
    bool m_optional;
};

// Arrays, mixed types and anything else: JSON typed into a line edit.
class JsonEditor final : public FieldEditor {
public:
    JsonEditor(bool optional, const std::function<void()>& changed)
        : m_edit(new QLineEdit)
        , m_optional(optional)
    {
        m_edit->setPlaceholderText(optional ? u"Optional JSON value"_s : u"JSON value"_s);
        QObject::connect(m_edit, &QLineEdit::textChanged, m_edit, [changed] { changed(); });
    }
    QWidget* widget() const override { return m_edit; }
    std::optional<QJsonValue> value() const override
    {
        const QString text = m_edit->text().trimmed();
        if (text.isEmpty())
            return m_optional ? std::nullopt : std::optional(QJsonValue());
        return parse(text).value_or(QJsonValue(text));
    }
    void setValue(const std::optional<QJsonValue>& value) override
    {
        m_edit->setText(value ? display(*value) : QString());
    }
    QString inputError() const override
    {
        const QString text = m_edit->text().trimmed();
        return text.isEmpty() || parse(text) ? QString() : u"Enter a valid JSON value, e.g. [1, 2] or \"text\"."_s;
    }

private:
    static std::optional<QJsonValue> parse(const QString& text)
    {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson((u'[' + text + u']').toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || document.array().size() != 1)
            return std::nullopt;
        return document.array().first();
    }

    QLineEdit* m_edit;
    bool m_optional;
};

}  // namespace

QString singleSchemaType(const QJsonObject& schema)
{
    const QJsonValue type = schema.value(u"type");
    if (type.isString())
        return type.toString();
    if (type.isArray() && type.toArray().size() == 1)
        return type.toArray().first().toString();
    return {};
}

std::unique_ptr<FieldEditor> createFieldEditor(const QJsonObject& schema, bool optional, std::function<void()> changed)
{
    if (schema.value(u"enum").isArray())
        return std::make_unique<EnumEditor>(schema.value(u"enum").toArray(), optional, changed);
    const QString type = singleSchemaType(schema);
    if (type == u"string")
        return std::make_unique<StringEditor>(optional, changed);
    if (type == u"integer")
        return std::make_unique<IntegerEditor>(schema, optional, changed);
    if (type == u"number")
        return std::make_unique<NumberEditor>(schema, optional, changed);
    if (type == u"boolean")
        return std::make_unique<BooleanEditor>(optional, changed);
    return std::make_unique<JsonEditor>(optional, changed);
}

}  // namespace mikserstudio::ui
