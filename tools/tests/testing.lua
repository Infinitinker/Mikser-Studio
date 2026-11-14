-- The tests of the scripts in tools/: plain Lua, run by CTest (tools/tests/CMakeLists.txt). A test file
-- puts its own folder and tools/ on package.path, requires this, names each test with testing.test,
-- checks with testing.equal and testing.truthy, and ends with os.exit(testing.finish()), which prints
-- the failures and exits with 1 if there are any.

local testing = {}

local failures = {}
local current = "(no test)"

--- A readable form of a value: tables with their keys in order, strings quoted.
local function show(value)
    if type(value) == "string" then
        return ("%q"):format(value)
    elseif type(value) ~= "table" then
        return tostring(value)
    end
    local keys = {}
    for key in pairs(value) do
        keys[#keys + 1] = key
    end
    table.sort(keys, function(a, b)
        return tostring(a) < tostring(b)
    end)
    local shown = {}
    for _, key in ipairs(keys) do
        shown[#shown + 1] = ("[%s] = %s"):format(show(key), show(value[key]))
    end
    return "{ " .. table.concat(shown, ", ") .. " }"
end

local function same(a, b)
    if type(a) ~= "table" or type(b) ~= "table" then
        return a == b
    end
    for key, value in pairs(a) do
        if not same(value, b[key]) then
            return false
        end
    end
    for key in pairs(b) do
        if a[key] == nil then
            return false
        end
    end
    return true
end

local function fail(message)
    failures[#failures + 1] = ("%s: %s"):format(current, message)
end

--- Runs one test; an error in it counts as its failure.
function testing.test(name, body)
    current = name
    local ok, problem = pcall(body)
    if not ok then
        fail("error: " .. tostring(problem))
    end
end

--- Checks that two values are equal, tables by their contents.
function testing.equal(actual, expected)
    if not same(actual, expected) then
        fail(("expected %s, got %s"):format(show(expected), show(actual)))
    end
end

--- Checks that a value is neither false nor nil.
function testing.truthy(value, what)
    if not value then
        fail(("expected %s to hold"):format(what))
    end
end

--- Prints the failures and returns the exit code.
function testing.finish()
    for _, failure in ipairs(failures) do
        print("FAILED " .. failure)
    end
    if #failures > 0 then
        return 1
    end
    print("All tests passed.")
    return 0
end

return testing
