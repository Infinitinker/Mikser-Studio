-- check_includes.lua: the import directions, the include style, and files in folders it does not know.

local here = debug.getinfo(1, "S").source:match("^@(.-)[^/\\]*$")
package.path = here .. "?.lua;" .. here .. "../?.lua;" .. package.path
local testing = require("testing")
local check = require("check_includes")
local test, equal = testing.test, testing.equal

local function problems(path, contents, tracked)
    local files = {}
    for _, name in ipairs(tracked or {}) do
        files[name] = true
    end
    local found = {}
    for index, problem in ipairs(check.check_file(path, contents, files)) do
        found[index] = tostring(problem)
    end
    return found
end

test("a module includes the modules it depends on", function()
    equal(problems("src/ui/src/Shell.cpp", '#include "mikserstudio/core/CommandBus.h"\n'), {})
end)

test("an include against the directions is named", function()
    equal(problems("src/ui/src/Shell.cpp", '#include "mikserstudio/engine/MltRuntime.h"\n'), {
        'src/ui/src/Shell.cpp:1: "mikserstudio/engine/MltRuntime.h" goes from ui to engine',
    })
end)

test("public headers take quotes", function()
    equal(problems("src/ui/src/Shell.cpp", "#include <mikserstudio/core/CommandBus.h>\n"), {
        "src/ui/src/Shell.cpp:1: <mikserstudio/core/CommandBus.h> is a public header: include it in quotes",
    })
    equal(problems("src/ui/src/Shell.cpp", '#include "mikserstudio/scripting/Lua.h"\n'), {
        'src/ui/src/Shell.cpp:1: "mikserstudio/scripting/Lua.h" names no module',
    })
end)

test("a private header is found in the module", function()
    local tracked = { "src/ui/src/Logging.h" }
    equal(problems("src/ui/src/Shell.cpp", '#include "Logging.h"\n', tracked), {})
    equal(problems("src/app/Studio.cpp", '#include "Logging.h"\n', tracked), {
        'src/app/Studio.cpp:1: "Logging.h" is not one of app\'s own headers',
    })
end)

test("a header made from a template is found by its template", function()
    local tracked = { "src/lua/src/Prelude.h.in" }
    equal(problems("src/lua/src/LuaWorker.cpp", '#include "Prelude.h"\n', tracked), {})
    equal(problems("src/app/Studio.cpp", '#include "Prelude.h"\n', tracked), {
        'src/app/Studio.cpp:1: "Prelude.h" is not one of app\'s own headers',
    })
end)

test("the end-to-end tests include only the modules they name", function()
    local tracked = {
        "tests/services/ServicesTestSupport.h",
        "tests/ui/TestCommands.h",
        "tests/api/HttpTestSupport.h",
    }
    local path = "tests/e2e/AiRunTests.cpp"
    equal(problems(path, '#include "HttpTestSupport.h"\n', tracked), {})
    equal(problems(path, '#include "mikserstudio/api/ApiServer.h"\n'), {
        path .. ':1: "mikserstudio/api/ApiServer.h" goes from e2e to api',
    })
    equal(problems(path, "#include <QProcess>\n"), {})
    equal(problems(path, '#include "mikserstudio/services/AiEngine.h"\n'), {})
    equal(problems(path, '#include "ServicesTestSupport.h"\n', tracked), {})
    equal(problems(path, '#include "mikserstudio/ui/Shell.h"\n'), {
        path .. ':1: "mikserstudio/ui/Shell.h" goes from e2e to ui',
    })
    equal(problems(path, '#include "TestCommands.h"\n', tracked), {
        path .. ':1: "TestCommands.h" is not one of e2e\'s own headers',
    })
end)

test("the plugins' tests include the core and the Lua host", function()
    local tracked = { "tests/core/TestSupport.h" }
    local plugin_test = "tests/plugins/hello/HelloPluginTests.cpp"
    equal(problems(plugin_test, '#include "mikserstudio/core/PluginManager.h"\n'), {})
    equal(problems(plugin_test, '#include "mikserstudio/lua/LuaRuntime.h"\n'), {})
    equal(problems(plugin_test, '#include "TestSupport.h"\n', tracked), {})
    equal(problems(plugin_test, '#include "mikserstudio/ui/Shell.h"\n'), {
        plugin_test .. ':1: "mikserstudio/ui/Shell.h" goes from plugins to ui',
    })
end)

test("a file in a folder the check does not know is a problem", function()
    for _, path in ipairs({
        "tests/benchmarks/Bench.cpp",
        "plugins/examples/hello/native.cpp",
        "sdk/include/mikserstudio/plugin_api.h",
        "src/scripting/Lua.cpp",
        "tools/native/helper.cpp",
        "tests/Main.cpp",
    }) do
        equal(problems(path, "#include <QString>\n"), { path .. ": " .. check.UNKNOWN_FOLDER })
    end
end)

os.exit(testing.finish())
