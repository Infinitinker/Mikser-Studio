#include "mikserstudio/core/JsonSchema.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <cmath>
#include <optional>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

namespace {

QString escapePointerToken(QString token)
{
    token.replace(u'~', u"~0"_s);
    token.replace(u'/', u"~1"_s);
    return token;
}

QString childPointer(const QString& parent, const QString& key)
{
    return parent + u'/' + escapePointerToken(key);
}

bool isInteger(const QJsonValue& value)
{
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    return std::isfinite(number) && std::trunc(number) == number;
}

bool matchesType(const QJsonValue& value, const QString& type)
{
    if (type == u"string")
        return value.isString();
    if (type == u"number")
        return value.isDouble();
    if (type == u"integer")
        return isInteger(value);
    if (type == u"boolean")
        return value.isBool();
    if (type == u"object")
        return value.isObject();
    if (type == u"array")
        return value.isArray();
    if (type == u"null")
        return value.isNull();
    return false;
}

QString typeWithArticle(const QString& type)
{
    if (type == u"null")
        return type;
    if (type == u"integer" || type == u"object" || type == u"array")
        return u"an "_s + type;
    return u"a "_s + type;
}

bool jsonEqual(const QJsonValue& a, const QJsonValue& b)
{
    if (a.isDouble() && b.isDouble())
        return a.toDouble() == b.toDouble();
    if (a.type() != b.type())
        return false;
    if (a.isArray()) {
        const QJsonArray left = a.toArray();
        const QJsonArray right = b.toArray();
        if (left.size() != right.size())
            return false;
        for (qsizetype i = 0; i < left.size(); ++i) {
            if (!jsonEqual(left.at(i), right.at(i)))
                return false;
        }
        return true;
    }
    if (a.isObject()) {
        const QJsonObject left = a.toObject();
        const QJsonObject right = b.toObject();
        if (left.size() != right.size())
            return false;
        for (auto it = left.begin(); it != left.end(); ++it) {
            if (!right.contains(it.key()) || !jsonEqual(it.value(), right.value(it.key())))
                return false;
        }
        return true;
    }
    return a == b;
}

QString toDisplay(const QJsonValue& value)
{
    if (value.isString())
        return u'"' + value.toString() + u'"';
    if (isInteger(value) && std::abs(value.toDouble()) < 1e15)
        return QString::number(static_cast<qint64>(value.toDouble()));
    if (value.isDouble())
        return QString::number(value.toDouble(), 'g', 15);
    if (value.isBool())
        return value.toBool() ? u"true"_s : u"false"_s;
    if (value.isNull())
        return u"null"_s;
    const QJsonDocument document = value.isArray() ? QJsonDocument(value.toArray()) : QJsonDocument(value.toObject());
    return QString::fromUtf8(document.toJson(QJsonDocument::Compact));
}

qsizetype codePointCount(const QString& text)
{
    qsizetype count = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (!text.at(i).isLowSurrogate() || i == 0 || !text.at(i - 1).isHighSurrogate())
            ++count;
    }
    return count;
}

class Validator {
public:
    void validate(const QJsonValue& value, const QJsonObject& schema, const QString& pointer)
    {
        if (!checkType(value, schema, pointer))
            return;
        checkConstAndEnum(value, schema, pointer);
        if (value.isDouble())
            checkNumber(value.toDouble(), schema, pointer);
        else if (value.isString())
            checkString(value.toString(), schema, pointer);
        else if (value.isObject())
            checkObject(value.toObject(), schema, pointer);
        else if (value.isArray())
            checkArray(value.toArray(), schema, pointer);
    }

    [[nodiscard]] QStringList errors() const { return m_errors; }

private:
    void add(const QString& pointer, const QString& message)
    {
        m_errors.append((pointer.isEmpty() ? u"(root)"_s : pointer) + u": "_s + message);
    }

    bool checkType(const QJsonValue& value, const QJsonObject& schema, const QString& pointer)
    {
        const QJsonValue typeValue = schema.value(u"type");
        QStringList types;
        if (typeValue.isString()) {
            types.append(typeValue.toString());
        } else if (typeValue.isArray()) {
            for (const QJsonValue entry : typeValue.toArray())
                types.append(entry.toString());
        } else {
            return true;
        }
        for (const QString& type : std::as_const(types)) {
            if (matchesType(value, type))
                return true;
        }
        QStringList described;
        for (const QString& type : std::as_const(types))
            described.append(typeWithArticle(type));
        add(pointer, u"must be "_s + described.join(u" or "_s));
        return false;
    }

    void checkConstAndEnum(const QJsonValue& value, const QJsonObject& schema, const QString& pointer)
    {
        if (schema.contains(u"const") && !jsonEqual(value, schema.value(u"const")))
            add(pointer, u"must be "_s + toDisplay(schema.value(u"const")));

        const QJsonValue enumValue = schema.value(u"enum");
        if (!enumValue.isArray())
            return;
        const QJsonArray options = enumValue.toArray();
        for (const QJsonValue option : options) {
            if (jsonEqual(value, option))
                return;
        }
        QStringList described;
        for (const QJsonValue option : options)
            described.append(toDisplay(option));
        add(pointer, u"must be one of: "_s + described.join(u", "_s));
    }

    void checkNumber(double number, const QJsonObject& schema, const QString& pointer)
    {
        const auto limit = [&schema](QStringView keyword) -> std::optional<QJsonValue> {
            const QJsonValue bound = schema.value(keyword);
            return bound.isDouble() ? std::optional(bound) : std::nullopt;
        };
        if (const auto bound = limit(u"minimum"); bound && number < bound->toDouble())
            add(pointer, u"must be >= "_s + toDisplay(*bound));
        if (const auto bound = limit(u"maximum"); bound && number > bound->toDouble())
            add(pointer, u"must be <= "_s + toDisplay(*bound));
        if (const auto bound = limit(u"exclusiveMinimum"); bound && number <= bound->toDouble())
            add(pointer, u"must be > "_s + toDisplay(*bound));
        if (const auto bound = limit(u"exclusiveMaximum"); bound && number >= bound->toDouble())
            add(pointer, u"must be < "_s + toDisplay(*bound));
    }

    void checkString(const QString& text, const QJsonObject& schema, const QString& pointer)
    {
        const qsizetype length = codePointCount(text);
        const QJsonValue minLength = schema.value(u"minLength");
        if (minLength.isDouble() && static_cast<double>(length) < minLength.toDouble())
            add(pointer, u"must be at least %1 characters long"_s.arg(toDisplay(minLength)));
        const QJsonValue maxLength = schema.value(u"maxLength");
        if (maxLength.isDouble() && static_cast<double>(length) > maxLength.toDouble())
            add(pointer, u"must be at most %1 characters long"_s.arg(toDisplay(maxLength)));

        const QJsonValue pattern = schema.value(u"pattern");
        if (!pattern.isString())
            return;
        const QRegularExpression expression(pattern.toString());
        if (!expression.isValid()) {
            add(pointer, u"the schema pattern \"%1\" is not a valid regular expression (%2)"_s.arg(
                             pattern.toString(), expression.errorString()));
            return;
        }
        if (!expression.match(text).hasMatch())
            add(pointer, u"must match the pattern %1"_s.arg(pattern.toString()));
    }

    void checkObject(const QJsonObject& object, const QJsonObject& schema, const QString& pointer)
    {
        for (const QJsonValue name : schema.value(u"required").toArray()) {
            if (name.isString() && !object.contains(name.toString()))
                add(childPointer(pointer, name.toString()), u"is required"_s);
        }

        const QJsonObject properties = schema.value(u"properties").toObject();
        const QJsonValue additional = schema.value(u"additionalProperties");
        for (auto it = object.begin(); it != object.end(); ++it) {
            const QString child = childPointer(pointer, it.key());
            if (properties.contains(it.key())) {
                validate(it.value(), properties.value(it.key()).toObject(), child);
            } else if (additional.isObject()) {
                validate(it.value(), additional.toObject(), child);
            } else if (additional.isBool() && !additional.toBool()) {
                const QStringList allowed = properties.keys();
                add(child, allowed.isEmpty()
                               ? u"is not allowed; this object takes no properties"_s
                               : u"is not allowed; allowed properties are: %1"_s.arg(allowed.join(u", "_s)));
            }
        }
    }

    void checkArray(const QJsonArray& array, const QJsonObject& schema, const QString& pointer)
    {
        const auto count = static_cast<double>(array.size());
        const QJsonValue minItems = schema.value(u"minItems");
        if (minItems.isDouble() && count < minItems.toDouble())
            add(pointer, u"must contain at least %1 items"_s.arg(toDisplay(minItems)));
        const QJsonValue maxItems = schema.value(u"maxItems");
        if (maxItems.isDouble() && count > maxItems.toDouble())
            add(pointer, u"must contain at most %1 items"_s.arg(toDisplay(maxItems)));

        const QJsonValue items = schema.value(u"items");
        if (!items.isObject())
            return;
        const QJsonObject itemSchema = items.toObject();
        for (qsizetype i = 0; i < array.size(); ++i)
            validate(array.at(i), itemSchema, pointer + u'/' + QString::number(i));
    }

    QStringList m_errors;
};

}  // namespace

QStringList JsonSchema::validate(const QJsonValue& value, const QJsonObject& schema)
{
    Validator validator;
    validator.validate(value, schema, QString());
    return validator.errors();
}

QJsonObject JsonSchema::applyDefaults(const QJsonObject& value, const QJsonObject& schema)
{
    QJsonObject result = value;
    const QJsonObject properties = schema.value(u"properties").toObject();
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        const QJsonObject propertySchema = it.value().toObject();
        if (!result.contains(it.key())) {
            if (!propertySchema.contains(u"default"))
                continue;
            const QJsonValue fallback = propertySchema.value(u"default");
            result.insert(it.key(),
                          fallback.isObject() ? applyDefaults(fallback.toObject(), propertySchema) : fallback);
        } else if (const QJsonValue present = result.value(it.key()); present.isObject()) {
            result.insert(it.key(), applyDefaults(present.toObject(), propertySchema));
        }
    }
    return result;
}

}  // namespace mikserstudio::core
