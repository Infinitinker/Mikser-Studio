// Plugin manifests: a valid one is parsed with defaults, errors name the field, the entry and the
// permissions and network entries are checked, panel contributions are checked, and native and Python
// plugins are refused.
#include "TestSupport.h"

#include "mikserstudio/core/NetworkEntry.h"
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
        "entry": "src/main.lua",
        "permissions": ["project.read", "ui"],
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
    CHECK(parsed->entry == u"src/main.lua"_s);
    CHECK(parsed->permissions == QStringList{u"project.read"_s, u"ui"_s});
    CHECK(parsed->settingsSchema.contains(u"properties"_s));
    CHECK(parsed->settingsSection.isEmpty());
    CHECK(parsed->directory == u"D:/plugins/hello"_s);
    REQUIRE(parsed->panels.size() == 2);
    CHECK(parsed->panels[0].id == u"greeter"_s);
    CHECK(parsed->panels[0].qml == u"qml/hello.qml"_s);
    CHECK(parsed->panels[0].defaultArea == u"left"_s);
    CHECK(parsed->panels[1].defaultArea == u"right"_s);

    QJsonObject minimal = without(u"panels"_s);
    minimal.remove(u"permissions"_s);
    const auto parsedMinimal = parsePluginManifest(minimal, u"D:/p"_s);
    REQUIRE(parsedMinimal);
    CHECK(parsedMinimal->panels.isEmpty());
    CHECK(parsedMinimal->permissions.isEmpty());
}

TEST_CASE("Manifest errors name the field", "[plugins][manifest]")
{
    for (const QString& key : {u"id"_s, u"name"_s, u"version"_s, u"apiVersion"_s, u"entry"_s})
        CHECK(errorFor(without(key)).contains(u"/%1: is required"_s.arg(key)));

    CHECK(errorFor(with(u"id"_s, u"Hello"_s)).contains(u"/id"_s));
    CHECK(errorFor(with(u"id"_s, u"hello"_s)).contains(u"/id"_s));
    CHECK(errorFor(with(u"version"_s, u"1.0"_s)).contains(u"/version"_s));
    CHECK(errorFor(with(u"version"_s, u"01.0.0"_s)).contains(u"/version"_s));
    CHECK(errorFor(with(u"apiVersion"_s, 2)).contains(u"API version 2"_s));
    CHECK(errorFor(with(u"apiVersion"_s, 1.5)).contains(u"/apiVersion"_s));
    CHECK(errorFor(with(u"settingsSchema"_s, json(R"({"type": "array"})"))).contains(u"/settingsSchema"_s));
    CHECK(errorFor(with(u"settingsSchema"_s, u"x"_s)).contains(u"/settingsSchema"_s));
    CHECK(errorFor(with(u"settingsSection"_s, u""_s)).contains(u"/settingsSection"_s));
    const auto sectioned = parsePluginManifest(with(u"settingsSection"_s, u"AI tools"_s), u"D:/p"_s);
    REQUIRE(sectioned);
    CHECK(sectioned->settingsSection == u"AI tools"_s);
}

TEST_CASE("The entry is a Lua file inside the plugin folder", "[plugins][manifest]")
{
    CHECK(errorFor(with(u"entry"_s, u"main.lua"_s)).isEmpty());
    for (const QString& entry :
         {u"../evil.lua"_s, u"/abs/main.lua"_s, u"C:/main.lua"_s, u"src\\main.lua"_s, u"hello"_s, u"main.dll"_s})
        CHECK(errorFor(with(u"entry"_s, entry)).startsWith(u"/entry: must be a Lua file"_s));
}

TEST_CASE("Permissions are the ones the app knows, each listed once", "[plugins][manifest]")
{
    CHECK(errorFor(with(u"permissions"_s, QJsonArray{u"project.read"_s, u"teleport"_s}))
              .startsWith(u"/permissions/1: “teleport” is not a permission; use one of project.read"_s));
    CHECK(errorFor(with(u"permissions"_s, QJsonArray{u"ui"_s, u"ui"_s})) == u"/permissions/1: “ui” is listed twice"_s);
    CHECK(errorFor(with(u"permissions"_s, QJsonArray{1})).contains(u"/permissions/0"_s));
    CHECK(errorFor(with(u"capabilities"_s, QJsonArray{u"commands"_s})).startsWith(u"/capabilities: replaced by"_s));
}

TEST_CASE("Network entries are host:port, need the network permission and default to none", "[plugins][manifest]")
{
    CHECK(parsePluginManifest(validManifest(), u"D:/plugins/hello"_s)->network.isEmpty());

    QJsonObject manifest = with(u"permissions"_s, QJsonArray{u"network"_s});
    manifest.insert(u"network"_s, QJsonArray{u"127.0.0.1:11434"_s, u"Api.Example.com:443"_s, u"[::1]:*"_s, u"*:*"_s});
    const auto parsed = parsePluginManifest(manifest, u"D:/plugins/hello"_s);
    REQUIRE(parsed);
    CHECK(parsed->network == QStringList{u"127.0.0.1:11434"_s, u"api.example.com:443"_s, u"[::1]:*"_s, u"*:*"_s});

    for (const QString& entry : {u"example.com"_s, u"example.com:0"_s, u"example.com:70000"_s, u"::1:80"_s,
                                 u"http://example.com:80"_s, u"*.example.com:443"_s, u"exa mple.com:80"_s}) {
        manifest.insert(u"network"_s, QJsonArray{entry});
        INFO(entry.toStdString());
        CHECK(errorFor(manifest).startsWith(u"/network/0: “%1” is not a network entry"_s.arg(entry)));
    }
    CHECK(errorFor(with(u"network"_s, QJsonArray{u"*:443"_s})).contains(u"doesn’t ask for the “network” permission"_s));
}

TEST_CASE("Network entries name their host and port", "[plugins][manifest]")
{
    const auto any = parseNetworkEntry(u"*:*"_s);
    REQUIRE(any);
    CHECK(any->host == u"*"_s);
    CHECK_FALSE(any->port.has_value());
    const auto ipv6 = parseNetworkEntry(u"[::1]:11434"_s);
    REQUIRE(ipv6);
    CHECK(ipv6->host == u"::1"_s);
    CHECK(ipv6->port == 11434);
    CHECK(parseNetworkEntry(u" LocalHost:8188 "_s)->host == u"localhost"_s);
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
    CHECK(errorFor(panels(R"({"p": [{"id": "a", "title": "A", "qml": "a.qml", "extra": 1}]})"))
              .contains(u"/panels/0/extra"_s));
    CHECK(
        errorFor(
            panels(R"({"p": [{"id": "a", "title": "A", "qml": "a.qml"}, {"id": "a", "title": "B", "qml": "b.qml"}]})"))
            .contains(u"/panels/1/id"_s));
}

TEST_CASE("Native and Python plugins are refused with the reason", "[plugins][manifest]")
{
    for (const QString& kind : {u"native"_s, u"python"_s, u"lua"_s})
        CHECK(errorFor(with(u"kind"_s, kind)).startsWith(u"/kind: Mikser Studio runs Lua plugins only"_s));
}
