#include "TestSupport.h"

#include "mikserstudio/core/PluginManager.h"

#include <catch2/catch_test_macros.hpp>

#include <QJsonArray>

using namespace Qt::StringLiterals;
using namespace mikserstudio::core;
using mikserstudio::tests::json;

namespace {

QJsonObject validManifest()
{
    return json(R"({
        "id": "mikserstudio.examples.hello",
        "name": "Hello",
        "version": "1.2.3-beta.1+build.5",
        "apiVersion": 1,
        "kind": "native",
        "entry": "hello",
        "capabilities": ["commands", "panels"],
        "settingsSchema": {"type": "object", "properties": {"greeting": {"type": "string", "default": "Hello"}}},
        "panels": [{"id": "greeter", "title": "Greeter", "qml": "qml/hello.qml", "defaultArea": "left"},
                   {"id": "other.panel", "title": "Other", "qml": "other.qml"}]
    })");
}

QString errorFor(const QJsonObject& manifest)
{
    const auto parsed = parsePluginManifest(manifest, u"D:/plugins/hello"_s);
    return parsed ? QString() : parsed.error();
}

QJsonObject with(const QString& key, const QJsonValue& value)
{
    QJsonObject manifest = validManifest();
    manifest.insert(key, value);
    return manifest;
}

QJsonObject without(const QString& key)
{
    QJsonObject manifest = validManifest();
    manifest.remove(key);
    return manifest;
}

}  // namespace

TEST_CASE("A valid manifest is parsed with defaults", "[plugins][manifest]")
{
    const auto parsed = parsePluginManifest(validManifest(), u"D:/plugins/hello"_s);
    REQUIRE(parsed);
    CHECK(parsed->id == u"mikserstudio.examples.hello"_s);
    CHECK(parsed->name == u"Hello"_s);
    CHECK(parsed->version == u"1.2.3-beta.1+build.5"_s);
    CHECK(parsed->description.isEmpty());
    CHECK(parsed->apiVersion == 1);
    CHECK(parsed->kind == u"native"_s);
    CHECK(parsed->entry == u"hello"_s);
    CHECK(parsed->capabilities == QStringList{u"commands"_s, u"panels"_s});
    CHECK(parsed->settingsSchema.contains(u"properties"_s));
    CHECK(parsed->directory == u"D:/plugins/hello"_s);
    REQUIRE(parsed->panels.size() == 2);
    CHECK(parsed->panels[0].id == u"greeter"_s);
    CHECK(parsed->panels[0].qml == u"qml/hello.qml"_s);
    CHECK(parsed->panels[0].defaultArea == u"left"_s);
    CHECK(parsed->panels[1].defaultArea == u"right"_s);

    const auto minimal = parsePluginManifest(without(u"panels"_s), u"D:/p"_s);
    REQUIRE(minimal);
    CHECK(minimal->panels.isEmpty());
}

TEST_CASE("Manifest errors name the field", "[plugins][manifest]")
{
    for (const QString& key : {u"id"_s, u"name"_s, u"version"_s, u"apiVersion"_s, u"kind"_s, u"entry"_s})
        CHECK(errorFor(without(key)).contains(u"/%1: is required"_s.arg(key)));

    CHECK(errorFor(with(u"id"_s, u"Hello"_s)).contains(u"/id"_s));
    CHECK(errorFor(with(u"id"_s, u"hello"_s)).contains(u"/id"_s));
    CHECK(errorFor(with(u"version"_s, u"1.0"_s)).contains(u"/version"_s));
    CHECK(errorFor(with(u"version"_s, u"01.0.0"_s)).contains(u"/version"_s));
    CHECK(errorFor(with(u"kind"_s, u"lua"_s)).contains(u"/kind"_s));
    CHECK(errorFor(with(u"apiVersion"_s, 2)).contains(u"API version 2"_s));
    CHECK(errorFor(with(u"apiVersion"_s, 1.5)).contains(u"/apiVersion"_s));
    CHECK(errorFor(with(u"entry"_s, u"../evil"_s)).contains(u"/entry"_s));
    CHECK(errorFor(with(u"entry"_s, u"sub/hello"_s)).contains(u"/entry"_s));
    CHECK(errorFor(with(u"capabilities"_s, QJsonArray{1})).contains(u"/capabilities/0"_s));
    CHECK(errorFor(with(u"settingsSchema"_s, json(R"({"type": "array"})"))).contains(u"/settingsSchema"_s));
    CHECK(errorFor(with(u"settingsSchema"_s, u"x"_s)).contains(u"/settingsSchema"_s));
}

TEST_CASE("Panel contributions are checked", "[plugins][manifest]")
{
    const auto panels = [](std::string_view text) { return with(u"panels"_s, json(text).value(u"p")); };
    CHECK(errorFor(panels(R"({"p": [{"id": "a", "title": "A"}]})")).contains(u"/panels/0/qml: is required"_s));
    CHECK(errorFor(panels(R"({"p": [{"id": "a", "title": "A", "qml": "a.qml", "defaultArea": "top"}]})"))
              .contains(u"/panels/0/defaultArea"_s));
    CHECK(errorFor(panels(R"({"p": [{"id": "A b", "title": "A", "qml": "a.qml"}]})")).contains(u"/panels/0/id"_s));
    CHECK(errorFor(panels(R"({"p": [{"id": "a", "title": "A", "qml": "../a.qml"}]})")).contains(u"/panels/0/qml"_s));
    CHECK(errorFor(panels(R"({"p": [{"id": "a", "title": "A", "qml": "C:/a.qml"}]})")).contains(u"/panels/0/qml"_s));
    CHECK(errorFor(panels(R"({"p": [{"id": "a", "title": "A", "qml": "/abs/a.qml"}]})")).contains(u"/panels/0/qml"_s));
    CHECK(errorFor(panels(R"({"p": [{"id": "a", "title": "A", "qml": "a.qml", "extra": 1}]})")).contains(u"/panels/0/extra"_s));
    CHECK(errorFor(panels(R"({"p": [{"id": "a", "title": "A", "qml": "a.qml"}, {"id": "a", "title": "B", "qml": "b.qml"}]})"))
              .contains(u"/panels/1/id"_s));
}

TEST_CASE("Python plugin entries use module:attribute", "[plugins][manifest]")
{
    QJsonObject manifest = with(u"kind"_s, u"python"_s);
    manifest.insert(u"entry"_s, u"mikserstudio_upscale.plugin:Plugin"_s);
    CHECK(errorFor(manifest).isEmpty());
    manifest.insert(u"entry"_s, u"hello"_s);
    CHECK(errorFor(manifest).contains(u"/entry"_s));
}
