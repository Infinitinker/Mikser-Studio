-- licenses.lua: reading SPDX expressions, and which of them the allow list lets a release ship.

local here = debug.getinfo(1, "S").source:match("^@(.-)[^/\\]*$")
package.path = here .. "?.lua;" .. here .. "../?.lua;" .. package.path
local testing = require("testing")
local licenses = require("licenses")
local test, equal, truthy = testing.test, testing.equal, testing.truthy

local function set(...)
    local names = {}
    for _, name in ipairs({ ... }) do
        names[name] = true
    end
    return names
end

test("exceptions and pacman's prefixes are dropped", function()
    equal(
        licenses.licenses_of("GPL-3.0-or-later WITH GCC-exception-3.1 AND GFDL-1.3-or-later"),
        set("GPL-3.0-or-later", "GFDL-1.3-or-later")
    )
    equal(
        licenses.licenses_of("LGPL-2.1-or-later AND documentation:spdx:GPL-3.0-or-later"),
        set("LGPL-2.1-or-later", "GPL-3.0-or-later")
    )
end)

test("one allowed alternative is enough", function()
    truthy(licenses.is_allowed("LGPL-2.1-or-later OR MPL-1.1"), "LGPL-2.1-or-later OR MPL-1.1 allowed")
    truthy(licenses.is_allowed("BSD-3-Clause OR GPL-2.0-or-later"), "BSD-3-Clause OR GPL-2.0-or-later allowed")
    truthy(not licenses.is_allowed("MPL-1.1 OR GPL-2.0-only"), "MPL-1.1 OR GPL-2.0-only refused")
end)

test("AND binds tighter than OR", function()
    truthy(licenses.is_allowed("MPL-1.1 AND MIT OR Zlib"), "MPL-1.1 AND MIT OR Zlib allowed")
    truthy(not licenses.is_allowed("MIT AND GPL-2.0-only OR MPL-1.1"), "MIT AND GPL-2.0-only OR MPL-1.1 refused")
    equal(licenses.alternatives("GPL3, LGPL"), { set("GPL3", "LGPL") })
end)

test("licenses that cannot join the GPL-3.0 are refused", function()
    for _, expression in ipairs({ "GPL-2.0-only", "AFL-2.1", "MPL-1.1", "custom", "" }) do
        truthy(not licenses.is_allowed(expression), ("%q refused"):format(expression))
    end
end)

test("every exception has a reason", function()
    for name, reason in pairs(licenses.EXCEPTIONS) do
        truthy(reason:match("^%s*(.-)%s*$"):sub(-1) == ".", name .. "'s reason ends with a full stop")
    end
end)

os.exit(testing.finish())
