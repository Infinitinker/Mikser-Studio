-- Checks that the C++ code includes the modules' headers only in the directions "Import directions"
-- in docs/architecture.md writes down:
--
-- - a module includes the public headers ("mikserstudio/<module>/...", in quotes) only of itself and
--   of the modules it depends on, as ALLOWED lists them, and the plugin ABI (<mikserstudio/plugin_api.h>,
--   in angle brackets, as a plugin built apart from Mikser Studio includes it) only where ALLOWED names
--   sdk;
-- - any other header in quotes ("Logging.h") is the module's own: it is found next to the including
--   file, in the module's src/ or in its public include folder. A module's tests may include the
--   private headers of the module they test, and the tests' shared support files;
-- - the tests in a folder of TEST_FOLDERS include only the modules it names, and the shared support
--   files of those modules' tests;
-- - every C++ file is in a folder the check knows: sdk/, plugins/, src/<module>/, tests/<module>/ or a
--   folder of TEST_FOLDERS. A file anywhere else is a problem, so a new folder is never skipped.
--
-- Run it from the repository root:
--     lua5.4 tools/check_includes.lua
-- It names every include that goes the wrong way, or that it cannot find, and every file in a folder it
-- does not know, and then exits with 1.

package.path = debug.getinfo(1, "S").source:match("^@(.-)[^/\\]*$") .. "?.lua;" .. package.path
local process = require("process")
local text = require("text")

local check = {}

--- The modules, each with its folder: sdk/ and src/<module>/.
check.MODULES = { "sdk", "core", "engine", "api", "services", "ui", "app", "mcp" }
--- The modules each module may include from, besides itself. plugins/ holds the bundled plugins, which
--- see only the plugin ABI. mcp, the MCP bridge, drives the app only through its local API, so it
--- includes no other module.
check.ALLOWED = {
    sdk = {},
    core = { "sdk" },
    engine = { "core" },
    api = { "core" },
    services = { "core" },
    ui = { "core" },
    app = { "core", "engine", "api", "services", "ui" },
    mcp = {},
    plugins = { "sdk" },
}
--- The folders of tests/ that test no single module, with the modules they may include. The end-to-end
--- tests run the app and the MCP bridge as processes, with the services tests' fake AI tools; the
--- bundled plugins' tests load them through the core's PluginManager.
check.TEST_FOLDERS = {
    e2e = { "core", "services" },
    plugins = { "core" },
}
check.SOURCES = { [".cpp"] = true, [".h"] = true, [".hpp"] = true }
check.UNKNOWN_FOLDER = "is in a folder check_includes.lua does not know: add it to MODULES or TEST_FOLDERS"

local function contains(list, value)
    for _, item in ipairs(list) do
        if item == value then
            return true
        end
    end
    return false
end

local function parts(path)
    local found = {}
    for part in path:gmatch("[^/]+") do
        found[#found + 1] = part
    end
    return found
end

--- A path as Python's PurePosixPath writes it: without empty and "." parts.
local function normalize(path)
    local kept = {}
    for _, part in ipairs(parts(path)) do
        if part ~= "." then
            kept[#kept + 1] = part
        end
    end
    local joined = table.concat(kept, "/")
    if path:sub(1, 1) == "/" then
        return "/" .. joined
    end
    return joined == "" and "." or joined
end

local function join(folder, name)
    if name:sub(1, 1) == "/" then
        return normalize(name)
    end
    return normalize(folder .. "/" .. name)
end

local function parent(path)
    return path:match("^(.*)/[^/]*$") or "."
end

--- Whether path a sorts before path b, part by part, as Python sorts paths.
local function before(a, b)
    local left, right = parts(a), parts(b)
    for index = 1, math.min(#left, #right) do
        if left[index] ~= right[index] then
            return left[index] < right[index]
        end
    end
    return #left < #right
end

local function suffix(path)
    local name = path:match("[^/]*$")
    local dot = name:match("^.+()%.[^.]*$")
    return dot and name:sub(dot) or ""
end

local Problem = {}
Problem.__index = Problem

--- An include that goes the wrong way or cannot be found, or a file in a folder the check does not
--- know (line 0 and no include).
local function problem(path, line, include, reason)
    return setmetatable({ path = path, line = line, include = include, reason = reason }, Problem)
end

function Problem.__tostring(self)
    if self.include == "" then
        return ("%s: %s"):format(self.path, self.reason)
    end
    return ("%s:%d: %s %s"):format(self.path, self.line, self.include, self.reason)
end

--- The module a file belongs to, and whether it is one of that module's tests, or the folder of
--- TEST_FOLDERS it is in; nil in a folder the check does not know.
function check.module_of(path)
    local found = parts(path)
    if found[1] == "sdk" then
        return "sdk", false
    elseif found[1] == "plugins" then
        return "plugins", false
    elseif (found[1] == "src" or found[1] == "tests") and #found > 2 and contains(check.MODULES, found[2]) then
        return found[2], found[1] == "tests"
    elseif found[1] == "tests" and #found > 2 and check.TEST_FOLDERS[found[2]] then
        return found[2], true
    end
    return nil
end

--- Where a quoted include of this file may be found.
local function own_folders(module, is_test, path)
    if module == "sdk" then
        return { parent(path), "sdk/include" }
    end
    if is_test and check.TEST_FOLDERS[module] then
        local folders = { parent(path) }
        local named = { table.unpack(check.TEST_FOLDERS[module]) }
        table.sort(named)
        for _, name in ipairs(named) do
            folders[#folders + 1] = "tests/" .. name
        end
        return folders
    end
    if module == "plugins" then
        return { parent(path) }
    end
    local folders = { parent(path), "src/" .. module .. "/src", "src/" .. module }
    if is_test then
        folders[#folders + 1] = "tests/" .. module
    end
    return folders
end

--- The problems of one file, with its text and the set of tracked files.
function check.check_file(path, contents, tracked)
    local module, is_test = check.module_of(path)
    if not module then
        return { problem(path, 0, "", check.UNKNOWN_FOLDER) }
    end
    local allowed
    if is_test and check.TEST_FOLDERS[module] then
        allowed = check.TEST_FOLDERS[module]
    else
        allowed = { module, table.unpack(check.ALLOWED[module]) }
    end
    local problems = {}
    local function add(number, written, reason)
        problems[#problems + 1] = problem(path, number, written, reason)
    end
    for number, line in ipairs(text.lines(contents)) do
        local bracket, name = line:match('^%s*#%s*include%s*([<"])([^>"]+)[>"]')
        if bracket then
            local written = bracket == "<" and "<" .. name .. ">" or '"' .. name .. '"'
            local public_module = name:match("^mikserstudio/([a-z]+)/")
            local abi = not public_module and name == "mikserstudio/plugin_api.h"
            if public_module or abi then
                local target = public_module or "sdk"
                if abi and bracket == '"' then
                    add(number, written, "is the plugin ABI: include it in angle brackets")
                elseif public_module and bracket == "<" then
                    add(number, written, "is a public header: include it in quotes")
                elseif not contains(check.MODULES, target) then
                    add(number, written, "names no module")
                elseif not contains(allowed, target) then
                    add(number, written, ("goes from %s to %s"):format(module, target))
                end
            elseif bracket == '"' then
                -- An include in angle brackets is a system, Qt, MLT or other library header.
                local found = false
                for _, folder in ipairs(own_folders(module, is_test, path)) do
                    found = found or tracked[join(folder, name)] == true
                end
                if not found then
                    add(number, written, ("is not one of %s's own headers"):format(module))
                end
            end
        end
    end
    return problems
end

function check.main()
    local listed, ok = process.capture({ "git", "ls-files" })
    if not ok then
        io.stderr:write("git ls-files failed: run the check from the repository's root.\n")
        return 1
    end
    local tracked, paths = {}, {}
    for line in listed:gmatch("[^\n]+") do
        if not tracked[line] then
            tracked[line] = true
            paths[#paths + 1] = line
        end
    end
    table.sort(paths, before)
    local problems = {}
    for _, path in ipairs(paths) do
        if check.SOURCES[suffix(path)] then
            for _, found in ipairs(check.check_file(path, text.read(path), tracked)) do
                problems[#problems + 1] = found
            end
        end
    end
    for _, found in ipairs(problems) do
        print(tostring(found))
    end
    if #problems > 0 then
        print(('%d problem(s) with includes; see "Import directions" in docs/architecture.md.'):format(#problems))
        return 1
    end
    print("Every include goes the right way.")
    return 0
end

if arg and arg[0] and arg[0]:match("[^/\\]*$") == "check_includes.lua" then
    os.exit(check.main())
end

return check
