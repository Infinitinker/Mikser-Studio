#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/PluginManager.h"
#include "mikserstudio/core/UndoStack.h"

#include "PluginPanelBridge.h"
#include "TestCommands.h"

#include <catch2/catch_test_macros.hpp>

#include <QJsonDocument>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

TEST_CASE("PluginPanelBridge runs commands with the plugin QML contract", "[plugin-panels]")
{
    QTemporaryDir dir;
    mikserstudio::core::UndoStack undo;
    mikserstudio::core::CommandBus bus(undo);
    mikserstudio::core::PluginManager plugins(bus, dir.filePath(u"plugins"_s));
    int runs = 0;
    REQUIRE(mikserstudio::ui::test::registerTestCommand(
        bus,
        {.id = u"demo.rename"_s,
         .title = u"Rename"_s,
         .category = u"Demo"_s,
         .paramsSchema = QJsonDocument::fromJson(R"({"type": "object", "required": ["name"],
                                                     "properties": {"name": {"type": "string"}}})")
                             .object()},
        &runs));

    mikserstudio::ui::PluginPanelBridge bridge(bus, plugins, u"com.example.demo"_s);
    CHECK(bridge.pluginId() == u"com.example.demo"_s);
    CHECK(bridge.settings().isEmpty());  // The plugin is not installed: no settings.

    const QVariantMap ok = bridge.execute(u"demo.rename"_s, {{u"name"_s, u"Intro"_s}});
    CHECK(ok.value(u"ok"_s) == true);
    CHECK(ok.value(u"result"_s).toMap().value(u"ran"_s) == u"demo.rename"_s);
    CHECK(runs == 1);

    const QVariantMap invalid = bridge.execute(u"demo.rename"_s);
    CHECK(invalid.value(u"ok"_s) == false);
    const QVariantMap error = invalid.value(u"error"_s).toMap();
    CHECK(error.value(u"code"_s) == u"invalid_params"_s);
    CHECK_FALSE(error.value(u"message"_s).toString().isEmpty());
    CHECK(runs == 1);

    const QVariantMap unknown = bridge.execute(u"demo.missing"_s);
    CHECK(unknown.value(u"error"_s).toMap().value(u"code"_s) == u"unknown_command"_s);
}
