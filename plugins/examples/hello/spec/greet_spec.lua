-- The greet command: the plugin starts with its permissions, greets with the default name or the one
-- given, follows its greeting setting and refuses params of the wrong type.

local plugin = "mikserstudio.examples.hello"
local greet = plugin .. ".greet"

local function equal(actual, expected)
    if actual ~= expected then
        error(("expected %s, got %s"):format(tostring(expected), tostring(actual)), 2)
    end
end

local listed
for _, entry in ipairs(ms.run("plugins.list", {}).plugins) do
    if entry.id == plugin then
        listed = entry
    end
end
equal(listed.status, "active")
equal(table.concat(listed.permissions, ","), "project.read,ui")
equal(listed.settings.greeting, "Hello")

equal(ms.run(greet, {}).message, "Hello, world!")
equal(ms.run(greet, { name = 'Zoë "Z" \\ 😀' }).message, 'Hello, Zoë "Z" \\ 😀!')

ms.run("plugins.set_settings", { id = plugin, settings = { greeting = "Cześć" } })
equal(ms.run(greet, { name = "Mikser Studio" }).message, "Cześć, Mikser Studio!")

local ok, problem = pcall(ms.run, greet, { name = 3 })
equal(ok, false)
equal(problem.code, "invalid_params")
