// Values crossing into and out of a Lua state as JSON.
#include "LuaJson.h"

#include <catch2/catch_test_macros.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <lua.hpp>

#include <memory>

using namespace Qt::StringLiterals;
using mikserstudio::lua::kArrayMetatable;
using mikserstudio::lua::pushJson;
using mikserstudio::lua::toJson;

namespace {

struct State {
    State()
        : lua(luaL_newstate(), &lua_close)
    {
        luaL_openlibs(lua.get());
        luaL_newmetatable(lua.get(), kArrayMetatable);
        lua_setglobal(lua.get(), "array_mt");
    }

    // The JSON of what `expression` evaluates to.
    std::expected<QJsonValue, QString> evaluate(const char* expression) const
    {
        const QByteArray chunk = QByteArray("return ") + expression;
        REQUIRE(luaL_dostring(lua.get(), chunk.constData()) == LUA_OK);
        auto json = toJson(lua.get(), -1);
        lua_settop(lua.get(), 0);
        return json;
    }

    std::unique_ptr<lua_State, decltype(&lua_close)> lua;
};

QJsonValue parse(const char* json)
{
    return QJsonDocument::fromJson(json).object().value(u"v"_s);
}

}  // namespace

TEST_CASE("Lua tables become JSON objects and arrays")
{
    const State state;
    CHECK(*state.evaluate("{ a = 1, b = 'x', c = true, d = 1.5 }") ==
          parse(R"({"v": {"a": 1, "b": "x", "c": true, "d": 1.5}})"));
    CHECK(*state.evaluate("{ 1, 2, { x = 3 } }") == parse(R"({"v": [1, 2, {"x": 3}]})"));
    CHECK(*state.evaluate("{}") == QJsonValue(QJsonObject()));
    CHECK(*state.evaluate("setmetatable({}, array_mt)") == QJsonValue(QJsonArray()));
    CHECK(*state.evaluate("nil") == QJsonValue(QJsonValue::Null));
    CHECK(*state.evaluate("'héllo'") == QJsonValue(u"héllo"_s));
}

TEST_CASE("What JSON can't hold is refused, saying why")
{
    const State state;
    CHECK(state.evaluate("print").error().contains(u"function"_s));
    CHECK(state.evaluate("{ [1] = 1, [3] = 3 }").error().contains(u"holes"_s));
    CHECK(state.evaluate("{ 1, a = 2 }").error().contains(u"mixes"_s));
    CHECK(state.evaluate("{ [true] = 1 }").error().contains(u"boolean"_s));
    CHECK(state.evaluate("0/0").error().contains(u"can't hold"_s));
    CHECK(state.evaluate("(function() local t = {} t.t = t return t end)()").error().contains(u"holds itself"_s));
}

TEST_CASE("JSON pushed into Lua comes back the same, empty arrays included")
{
    const State state;
    const QJsonValue value = parse(
        R"({"v": {"list": [], "items": [1, "two", null, {"x": 1}], "n": 9007199254740993.5}})");
    pushJson(state.lua.get(), value);
    const auto back = toJson(state.lua.get(), -1);
    REQUIRE(back);
    CHECK(back->toObject().value(u"list"_s) == QJsonValue(QJsonArray()));
    CHECK(back->toObject().value(u"items"_s) == value.toObject().value(u"items"_s));
    CHECK(back->toObject().value(u"n"_s).toDouble() == value.toObject().value(u"n"_s).toDouble());

    // Whole numbers arrive as integers.
    pushJson(state.lua.get(), QJsonValue(42));
    CHECK(lua_isinteger(state.lua.get(), -1));
}
