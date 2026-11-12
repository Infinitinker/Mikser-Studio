// The bundled hello plugin: it is discovered from its manifest, activates, greets and follows its settings.
#include "TestSupport.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/PluginManager.h"
#include "mikserstudio/core/UndoStack.h"

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;
using namespace mikserstudio::core;
using mikserstudio::tests::json;

TEST_CASE("The bundled hello plugin is discovered, greets and follows its settings", "[plugins][hello]")
{
    QTemporaryDir settings;
    UndoStack stack;
    CommandBus bus(stack);
    PluginManager manager(bus, settings.path());
    manager.addSearchPath(QString::fromUtf8(MIKSERSTUDIO_HELLO_PLUGIN_ROOT));
    manager.discover();

    const PluginState* hello = manager.plugin(u"mikserstudio.examples.hello"_s);
    REQUIRE(hello);
    CHECK(hello->status == PluginStatus::Discovered);
    CHECK(hello->manifest.name == u"Hello"_s);
    REQUIRE(hello->manifest.panels.size() == 1);
    const PanelContribution& panel = hello->manifest.panels[0];
    CHECK(panel.id == u"greeter"_s);
    CHECK(panel.title == u"Greeter"_s);
    CHECK(panel.qml == u"hello.qml"_s);
    CHECK(panel.defaultArea == u"right"_s);
    CHECK(QFile::exists(hello->manifest.directory + u'/' + panel.qml));
    CHECK(manager.settings(u"mikserstudio.examples.hello"_s) == json(R"({"greeting": "Hello"})"));

    REQUIRE(manager.activate(u"mikserstudio.examples.hello"_s));
    const CommandSpec* greet = bus.spec(u"mikserstudio.examples.hello.greet"_s);
    REQUIRE(greet);
    CHECK(greet->title == u"Say hello"_s);
    CHECK(greet->category == u"Examples"_s);
    CHECK(greet->readOnly);

    auto result = bus.execute(u"mikserstudio.examples.hello.greet"_s);
    REQUIRE(result);
    CHECK(*result == json(R"({"message": "Hello, world!"})"));

    result = bus.execute(u"mikserstudio.examples.hello.greet"_s, json(R"({"name": "Zoë \"Z\" \\ 😀"})"));
    REQUIRE(result);
    CHECK(result->value(u"message").toString() == u"Hello, Zoë \"Z\" \\ \U0001F600!"_s);

    REQUIRE(manager.setSettings(u"mikserstudio.examples.hello"_s, json(R"({"greeting": "Cześć"})")));
    result = bus.execute(u"mikserstudio.examples.hello.greet"_s, json(R"({"name": "Mikser Studio"})"));
    REQUIRE(result);
    CHECK(result->value(u"message").toString() == u"Cześć, Mikser Studio!"_s);

    const auto invalid = bus.execute(u"mikserstudio.examples.hello.greet"_s, json(R"({"name": 3})"));
    REQUIRE_FALSE(invalid);
    CHECK(invalid.error().code == CommandErrorCode::InvalidParams);

    manager.deactivate(u"mikserstudio.examples.hello"_s);
    CHECK_FALSE(bus.contains(u"mikserstudio.examples.hello.greet"_s));
}
