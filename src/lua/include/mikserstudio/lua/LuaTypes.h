// The types of the plugins' ms API for Lua language servers, which mikser-studio --print-lua-types
// writes and plugins/types/ms.lua keeps (docs/plugins.md, "Tooling").
#pragma once

#include <QString>

namespace mikserstudio::lua {

// The types, in LuaLS's annotations, with the app's permissions and error codes.
[[nodiscard]] QString luaTypes();

}  // namespace mikserstudio::lua
