// Values crossing between a Lua state and the rest of the app, as JSON copies.
#pragma once

#include <QJsonValue>
#include <QString>

#include <expected>

struct lua_State;

namespace mikserstudio::lua {

// The metatable (by its registry name) that marks a table as a JSON array, even an empty one.
inline constexpr auto kArrayMetatable = "ms.array";

// Pushes `value` onto the stack. Arrays get the array metatable; null array items are ms.null
// (a light userdata holding nullptr); null object members are left out.
void pushJson(lua_State* state, const QJsonValue& value);

// The value at `index` as JSON. Tables with the array metatable, and tables whose keys are 1..n,
// are arrays; other tables need string keys and are objects; an empty table is an object. nil
// and ms.null are null. Fails, saying why, for a function, a coroutine, a userdata, a number
// that isn't finite, a table that holds itself or one nested more than 64 deep.
[[nodiscard]] std::expected<QJsonValue, QString> toJson(lua_State* state, int index);

}  // namespace mikserstudio::lua
