// Converting between Lua values and JSON with the Lua C API.
#include "LuaJson.h"

#include <QJsonArray>
#include <QJsonObject>

#include <lua.hpp>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace mikserstudio::lua {

namespace {

constexpr int kMaxDepth = 64;

// Doubles hold every integer up to 2^53 exactly.
constexpr double kExactIntegerLimit = 9007199254740992.0;

void pushString(lua_State* state, const QString& text)
{
    const QByteArray utf8 = text.toUtf8();
    lua_pushlstring(state, utf8.constData(), static_cast<std::size_t>(utf8.size()));
}

std::expected<QJsonValue, QString> convert(lua_State* state, int index, int depth);

bool hasArrayMetatable(lua_State* state, int index)
{
    if (!lua_getmetatable(state, index))
        return false;
    luaL_getmetatable(state, kArrayMetatable);
    const bool marked = lua_rawequal(state, -1, -2) != 0;
    lua_pop(state, 2);
    return marked;
}

std::expected<QJsonValue, QString> convertArray(lua_State* state, int index, int depth, lua_Integer length)
{
    QJsonArray array;
    for (lua_Integer i = 1; i <= length; ++i) {
        lua_rawgeti(state, index, i);
        auto item = convert(state, lua_gettop(state), depth + 1);
        lua_pop(state, 1);
        if (!item)
            return item;
        array.append(*item);
    }
    return array;
}

std::expected<QJsonValue, QString> convertTable(lua_State* state, int index, int depth)
{
    if (depth > kMaxDepth)
        return std::unexpected(u"a table is nested more than %1 deep, or holds itself"_s.arg(kMaxDepth));
    if (hasArrayMetatable(state, index))
        return convertArray(state, index, depth, static_cast<lua_Integer>(lua_rawlen(state, index)));

    // Count the keys: all integers 1..n make an array, all strings an object.
    lua_Integer count = 0;
    lua_Integer largest = 0;
    bool strings = false;
    bool integers = false;
    lua_pushnil(state);
    while (lua_next(state, index) != 0) {
        lua_pop(state, 1);  // The value; the key stays for lua_next.
        ++count;
        if (lua_type(state, -1) == LUA_TSTRING) {
            strings = true;
        } else if (lua_isinteger(state, -1) && lua_tointeger(state, -1) >= 1) {
            integers = true;
            largest = std::max(largest, lua_tointeger(state, -1));
        } else {
            const QString type = QString::fromUtf8(luaL_typename(state, -1));
            lua_pop(state, 1);
            return std::unexpected(u"a table has a key of type %1; JSON objects need string keys"_s.arg(type));
        }
    }
    if (strings && integers)
        return std::unexpected(u"a table mixes string keys and array items"_s);
    if (integers) {
        if (largest != count)
            return std::unexpected(u"an array has holes; use ms.null for an empty item"_s);
        return convertArray(state, index, depth, count);
    }

    QJsonObject object;
    lua_pushnil(state);
    while (lua_next(state, index) != 0) {
        std::size_t length = 0;
        const char* key = lua_tolstring(state, -2, &length);
        auto value = convert(state, lua_gettop(state), depth + 1);
        lua_pop(state, 1);
        if (!value) {
            lua_pop(state, 1);
            return value;
        }
        object.insert(QString::fromUtf8(key, static_cast<qsizetype>(length)), *value);
    }
    return object;
}

std::expected<QJsonValue, QString> convert(lua_State* state, int index, int depth)
{
    switch (lua_type(state, index)) {
    case LUA_TNIL:
    case LUA_TNONE:
        return QJsonValue(QJsonValue::Null);
    case LUA_TBOOLEAN:
        return QJsonValue(lua_toboolean(state, index) != 0);
    case LUA_TNUMBER: {
        if (lua_isinteger(state, index))
            return QJsonValue(static_cast<qint64>(lua_tointeger(state, index)));
        const double number = lua_tonumber(state, index);
        if (!std::isfinite(number))
            return std::unexpected(u"JSON can't hold %1"_s.arg(number));
        return QJsonValue(number);
    }
    case LUA_TSTRING: {
        std::size_t length = 0;
        const char* text = lua_tolstring(state, index, &length);
        return QJsonValue(QString::fromUtf8(text, static_cast<qsizetype>(length)));
    }
    case LUA_TLIGHTUSERDATA:
        if (lua_touserdata(state, index) == nullptr)
            return QJsonValue(QJsonValue::Null);
        break;
    case LUA_TTABLE:
        return convertTable(state, lua_absindex(state, index), depth);
    default:
        break;
    }
    return std::unexpected(u"JSON can't hold a %1"_s.arg(QString::fromUtf8(luaL_typename(state, index))));
}

}  // namespace

void pushJson(lua_State* state, const QJsonValue& value)
{
    switch (value.type()) {
    case QJsonValue::Bool:
        lua_pushboolean(state, value.toBool() ? 1 : 0);
        return;
    case QJsonValue::Double: {
        const double number = value.toDouble();
        if (std::trunc(number) == number && std::abs(number) <= kExactIntegerLimit)
            lua_pushinteger(state, static_cast<lua_Integer>(number));
        else
            lua_pushnumber(state, number);
        return;
    }
    case QJsonValue::String:
        pushString(state, value.toString());
        return;
    case QJsonValue::Array: {
        const QJsonArray array = value.toArray();
        lua_createtable(state, static_cast<int>(array.size()), 0);
        lua_Integer i = 0;
        for (const QJsonValue item : array) {
            pushJson(state, item);
            lua_rawseti(state, -2, ++i);
        }
        luaL_setmetatable(state, kArrayMetatable);
        return;
    }
    case QJsonValue::Object: {
        const QJsonObject object = value.toObject();
        lua_createtable(state, 0, static_cast<int>(object.size()));
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            if (it.value().isNull() || it.value().isUndefined())
                continue;
            pushString(state, it.key());
            pushJson(state, it.value());
            lua_rawset(state, -3);
        }
        return;
    }
    case QJsonValue::Null:
    case QJsonValue::Undefined:
        lua_pushlightuserdata(state, nullptr);
        return;
    }
}

std::expected<QJsonValue, QString> toJson(lua_State* state, int index)
{
    luaL_checkstack(state, kMaxDepth * 3 + 10, "converting a value to JSON");
    return convert(state, lua_absindex(state, index), 0);
}

}  // namespace mikserstudio::lua
