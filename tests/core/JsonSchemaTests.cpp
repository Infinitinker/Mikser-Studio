#include "TestSupport.h"

#include "mikserstudio/core/JsonSchema.h"

#include <catch2/catch_test_macros.hpp>

#include <QJsonArray>

using namespace Qt::StringLiterals;
using mikserstudio::core::JsonSchema;
using mikserstudio::tests::json;

namespace {

QStringList check(const QJsonValue& value, std::string_view schema)
{
    return JsonSchema::validate(value, json(schema));
}

}  // namespace

TEST_CASE("JsonSchema checks every type", "[schema]")
{
    CHECK(check(u"x"_s, R"({"type": "string"})").isEmpty());
    CHECK(check(1.5, R"({"type": "number"})").isEmpty());
    CHECK(check(3, R"({"type": "number"})").isEmpty());
    CHECK(check(true, R"({"type": "boolean"})").isEmpty());
    CHECK(check(QJsonObject(), R"({"type": "object"})").isEmpty());
    CHECK(check(QJsonArray(), R"({"type": "array"})").isEmpty());
    CHECK(check(QJsonValue(QJsonValue::Null), R"({"type": "null"})").isEmpty());

    CHECK(check(3, R"({"type": "string"})") == QStringList{u"(root): must be a string"_s});
    CHECK(check(u"3"_s, R"({"type": "number"})") == QStringList{u"(root): must be a number"_s});
    CHECK(check(u"x"_s, R"({"type": "boolean"})") == QStringList{u"(root): must be a boolean"_s});
    CHECK(check(1, R"({"type": "object"})") == QStringList{u"(root): must be an object"_s});
    CHECK(check(1, R"({"type": "array"})") == QStringList{u"(root): must be an array"_s});
    CHECK(check(0, R"({"type": "null"})") == QStringList{u"(root): must be null"_s});
}

TEST_CASE("JsonSchema distinguishes integer from number", "[schema]")
{
    CHECK(check(4, R"({"type": "integer"})").isEmpty());
    CHECK(check(4.0, R"({"type": "integer"})").isEmpty());
    CHECK(check(-12, R"({"type": "integer"})").isEmpty());
    CHECK(check(4.5, R"({"type": "integer"})") == QStringList{u"(root): must be an integer"_s});
    CHECK_FALSE(check(u"4"_s, R"({"type": "integer"})").isEmpty());
    CHECK(check(4.5, R"({"type": "number"})").isEmpty());
}

TEST_CASE("JsonSchema accepts a list of types", "[schema]")
{
    CHECK(check(u"a"_s, R"({"type": ["string", "null"]})").isEmpty());
    CHECK(check(QJsonValue(QJsonValue::Null), R"({"type": ["string", "null"]})").isEmpty());
    CHECK(check(1, R"({"type": ["string", "null"]})") == QStringList{u"(root): must be a string or null"_s});
}

TEST_CASE("JsonSchema checks enum and const", "[schema]")
{
    CHECK(check(u"left"_s, R"({"enum": ["left", "right", 3]})").isEmpty());
    CHECK(check(3.0, R"({"enum": ["left", "right", 3]})").isEmpty());
    CHECK(check(u"up"_s, R"({"enum": ["left", "right", 3]})")
          == QStringList{u"(root): must be one of: \"left\", \"right\", 3"_s});
    CHECK(check(json(R"({"a": [1, {"b": 2}]})"), R"({"enum": [{"a": [1, {"b": 2}]}]})").isEmpty());
    CHECK_FALSE(check(json(R"({"a": [1, {"b": 3}]})"), R"({"enum": [{"a": [1, {"b": 2}]}]})").isEmpty());

    CHECK(check(u"v1"_s, R"({"const": "v1"})").isEmpty());
    CHECK(check(u"v2"_s, R"({"const": "v1"})") == QStringList{u"(root): must be \"v1\""_s});
}

TEST_CASE("JsonSchema checks numeric bounds", "[schema]")
{
    const auto schema = R"({"minimum": 0, "maximum": 10})";
    CHECK(check(0, schema).isEmpty());
    CHECK(check(10, schema).isEmpty());
    CHECK(check(-1, schema) == QStringList{u"(root): must be >= 0"_s});
    CHECK(check(11, schema) == QStringList{u"(root): must be <= 10"_s});

    const auto exclusive = R"({"exclusiveMinimum": 0, "exclusiveMaximum": 1.5})";
    CHECK(check(0.5, exclusive).isEmpty());
    CHECK(check(0, exclusive) == QStringList{u"(root): must be > 0"_s});
    CHECK(check(1.5, exclusive) == QStringList{u"(root): must be < 1.5"_s});
    CHECK(check(u"not a number"_s, schema).isEmpty());
}

TEST_CASE("JsonSchema checks string length in code points and patterns", "[schema]")
{
    const auto lengths = R"({"minLength": 2, "maxLength": 3})";
    CHECK(check(u"ab"_s, lengths).isEmpty());
    CHECK(check(u"a"_s, lengths) == QStringList{u"(root): must be at least 2 characters long"_s});
    CHECK(check(u"abcd"_s, lengths) == QStringList{u"(root): must be at most 3 characters long"_s});
    // Two emoji are four UTF-16 units but two characters.
    CHECK(check(QString::fromUtf8("\xF0\x9F\x98\x80\xF0\x9F\x98\x80"), lengths).isEmpty());

    CHECK(check(u"clip-12"_s, R"({"pattern": "^clip-[0-9]+$"})").isEmpty());
    CHECK(check(u"xx clip-12"_s, R"({"pattern": "clip-[0-9]+"})").isEmpty());
    CHECK(check(u"media-1"_s, R"({"pattern": "^clip-[0-9]+$"})")
          == QStringList{u"(root): must match the pattern ^clip-[0-9]+$"_s});
    const QStringList broken = check(u"x"_s, R"({"pattern": "("})");
    REQUIRE(broken.size() == 1);
    CHECK(broken[0].contains(u"not a valid regular expression"_s));
}

TEST_CASE("JsonSchema checks object properties, required and additionalProperties", "[schema]")
{
    const auto schema = R"({
        "type": "object",
        "properties": {"name": {"type": "string"}, "size": {"type": "integer"}},
        "required": ["name"],
        "additionalProperties": false
    })";
    CHECK(check(json(R"({"name": "a", "size": 2})"), schema).isEmpty());
    CHECK(check(json(R"({"size": 2})"), schema) == QStringList{u"/name: is required"_s});
    CHECK(check(json(R"({"name": 1})"), schema) == QStringList{u"/name: must be a string"_s});
    const QStringList extra = check(json(R"({"name": "a", "colour": "red"})"), schema);
    REQUIRE(extra.size() == 1);
    CHECK(extra[0].startsWith(u"/colour: is not allowed"_s));
    CHECK(extra[0].contains(u"name, size"_s));

    CHECK(check(json(R"({"x": 1})"), R"({"type": "object", "additionalProperties": false})")
          == QStringList{u"/x: is not allowed; this object takes no properties"_s});
    CHECK(check(json(R"({"x": 1, "y": "s"})"), R"({"type": "object", "additionalProperties": {"type": "integer"}})")
          == QStringList{u"/y: must be an integer"_s});
    CHECK(check(json(R"({"x": 1})"), R"({"type": "object"})").isEmpty());
}

TEST_CASE("JsonSchema checks arrays", "[schema]")
{
    const auto schema = R"({"type": "array", "items": {"type": "integer"}, "minItems": 1, "maxItems": 2})";
    CHECK(check(QJsonArray{1, 2}, schema).isEmpty());
    CHECK(check(QJsonArray{}, schema) == QStringList{u"(root): must contain at least 1 items"_s});
    CHECK(check(QJsonArray{1, 2, 3}, schema) == QStringList{u"(root): must contain at most 2 items"_s});
    CHECK(check(QJsonArray{1, u"x"_s}, schema) == QStringList{u"/1: must be an integer"_s});
}

TEST_CASE("JsonSchema reports nested JSON Pointers with escaping", "[schema]")
{
    const auto schema = R"({
        "type": "object",
        "properties": {
            "a/b": {"type": "object", "properties": {"m~n": {"type": "array", "items": {
                "type": "object", "properties": {"deep": {"type": "integer", "minimum": 1}}, "required": ["must"]
            }}}}
        }
    })";
    const QStringList problems = check(json(R"({"a/b": {"m~n": [{"deep": 1, "must": 0}, {"deep": 0}]}})"), schema);
    CHECK(problems == QStringList{u"/a~1b/m~0n/1/must: is required"_s, u"/a~1b/m~0n/1/deep: must be >= 1"_s});
}

TEST_CASE("JsonSchema reports every problem and ignores unknown keywords", "[schema]")
{
    const auto schema = R"({
        "type": "object",
        "title": "T", "description": "D", "x-custom": 1, "$comment": "c",
        "properties": {"a": {"type": "string"}, "b": {"minimum": 3}},
        "required": ["c"]
    })";
    CHECK(check(json(R"({"a": 1, "b": 1})"), schema).size() == 3);
    CHECK(check(json(R"({"a": "x", "b": 3, "c": null})"), schema).isEmpty());
}

TEST_CASE("JsonSchema applies defaults at the top level and in nested objects", "[schema]")
{
    const QJsonObject schema = json(R"({
        "type": "object",
        "properties": {
            "name": {"type": "string", "default": "world"},
            "count": {"type": "integer"},
            "style": {
                "type": "object",
                "default": {"bold": true},
                "properties": {"bold": {"type": "boolean"}, "size": {"type": "integer", "default": 12}}
            }
        }
    })");
    CHECK(JsonSchema::applyDefaults({}, schema) == json(R"({"name": "world", "style": {"bold": true, "size": 12}})"));
    CHECK(JsonSchema::applyDefaults(json(R"({"name": "you", "style": {"bold": false}})"), schema)
          == json(R"({"name": "you", "style": {"bold": false, "size": 12}})"));
    CHECK(JsonSchema::applyDefaults(json(R"({"count": 3})"), schema).value(u"count").toInt() == 3);
    CHECK(JsonSchema::applyDefaults(json(R"({"a": 1})"), {}) == json(R"({"a": 1})"));
}
