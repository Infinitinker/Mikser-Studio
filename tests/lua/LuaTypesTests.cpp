// The types of the ms API: every list filled in, and plugins/types/ms.lua as the app writes them.
#include "LuaTestSupport.h"

#include "mikserstudio/lua/LuaTypes.h"

#include <catch2/catch_test_macros.hpp>

#include <QFile>

using namespace Qt::StringLiterals;

TEST_CASE("The Lua types name the permissions and error codes", "[lua]")
{
    const QString types = mikserstudio::lua::luaTypes();
    CHECK(types.startsWith(u"---@meta\n"_s));
    CHECK_FALSE(types.contains(u'{' + u"permissions}"_s));
    CHECK_FALSE(types.contains(u'{' + u"error_codes}"_s));
    CHECK(types.contains(u"---@alias ms.permission \"project.read\"|\"project.write\"|"_s));
    CHECK(types.contains(u"\"needs_confirmation\"|\"canceled\"|"_s));
}

TEST_CASE("plugins/types/ms.lua is what mikser-studio --print-lua-types writes", "[lua]")
{
    QFile committed(QString::fromUtf8(MIKSERSTUDIO_LUA_TYPES_FILE));
    REQUIRE(committed.open(QIODevice::ReadOnly));
    INFO("If the ms API changed, run mikser-studio --print-lua-types > plugins/types/ms.lua");
    CHECK(QString::fromUtf8(committed.readAll()) == mikserstudio::lua::luaTypes());
}
