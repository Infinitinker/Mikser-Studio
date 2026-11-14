-- luacheck's settings for the scripts in tools/ ("Checks" in docs/development.md): Lua 5.4's standard
-- library and the command-line arguments the standalone interpreter gives a script. The scripts
-- require LuaFileSystem and lua-cjson, so neither is a global. Every warning fails.

std = "lua54"
