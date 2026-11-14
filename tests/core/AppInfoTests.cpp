// app.info names the Mikser Studio that runs and its version, for plugins and agents.
#include "mikserstudio/core/AppInfo.h"
#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/UndoStack.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>

using namespace Qt::StringLiterals;
using namespace mikserstudio::core;

TEST_CASE("app.info answers the application's name and version, read-only", "[core][app]")
{
    const QString name = QCoreApplication::applicationName();
    const QString version = QCoreApplication::applicationVersion();
    QCoreApplication::setApplicationName(u"Mikser Studio Dev"_s);
    QCoreApplication::setApplicationVersion(u"1.2.3"_s);

    UndoStack undo;
    CommandBus bus{undo};
    registerAppInfoCommand(bus);
    const auto* const spec = bus.spec(u"app.info"_s);
    REQUIRE(spec);
    CHECK(spec->readOnly);
    CHECK(spec->permission == u"project.read"_s);

    const auto result = bus.execute(u"app.info"_s, {});
    REQUIRE(result);
    CHECK(result->value(u"name").toString() == u"Mikser Studio Dev"_s);
    CHECK(result->value(u"version").toString() == u"1.2.3"_s);
    CHECK_FALSE(undo.canUndo());

    QCoreApplication::setApplicationName(name);
    QCoreApplication::setApplicationVersion(version);
}
