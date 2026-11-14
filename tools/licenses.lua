-- Checks the licenses of everything a Windows release of Mikser Studio ships from MSYS2
-- (tools/msys2_packages.lua) against the allow list. Mikser Studio is under the GPL-3.0-or-later, so a
-- shipped package must be usable under a license that can be combined with it: one of the alternatives
-- of its SPDX expression (joined by OR) must name only licenses on ALLOWED, or the package must be in
-- EXCEPTIONS, with the reason. It names every package that fails, and then exits with 1.
--
-- Run it from the repository root, with MSYS2's packages installed as docs/development.md describes:
--     lua5.4 tools/licenses.lua

package.path = debug.getinfo(1, "S").source:match("^@(.-)[^/\\]*$") .. "?.lua;" .. package.path

local licenses = {}

--- The SPDX identifiers of the licenses that can be combined with the GPL-3.0-or-later, as far as a
--- release ships them. CLAUDE.md's "Open source" lists them too.
licenses.ALLOWED = {}
for _, name in ipairs({
    -- Permissive licenses, which the GPL-3.0 can take in.
    "0BSD",
    "Apache-2.0",
    "BSD-2-Clause",
    "BSD-3-Clause",
    "BSD-3-Clause-Clear",
    "BSL-1.0",
    "CC0-1.0",
    "FTL",
    "ICU",
    "ISC",
    "MIT",
    "WTFPL",
    "Zlib",
    -- Copyleft licenses that allow, or are, the GPL-3.0.
    "GPL-2.0-or-later",
    "GPL-3.0-only",
    "GPL-3.0-or-later",
    "LGPL-2.0-or-later",
    "LGPL-2.1-or-later",
    "LGPL-3.0-only",
    "LGPL-3.0-or-later",
    "MPL-2.0",
    -- The packages' manuals and images: shipped with the libraries' licenses, not combined with code.
    "CC-BY-4.0",
    "GFDL-1.3-no-invariants-only",
    "GFDL-1.3-or-later",
}) do
    licenses.ALLOWED[name] = true
end

local UPSTREAM = "MSYS2's package installs no license text (a gap in THIRD-PARTY-NOTICES.md); the project releases it"
--- Packages allowed whatever their metadata says, each with the reason, after checking the license by
--- hand: those whose metadata names no SPDX license, or joins alternatives with AND.
licenses.EXCEPTIONS = {
    -- MSYS2 names no SPDX license, or none with a version: each checked in the text it installs.
    ["bzip2"] = "bzip2-1.0.6, a BSD-style license (its LICENSE).",
    ["fontconfig"] = "MIT-style licenses and public-domain code (its COPYING).",
    ["gsm"] = "A permissive license: any use, with the notice kept (its license.txt).",
    ["libpng"] = "libpng-2.0, a permissive license (its LICENSE).",
    ["libjpeg-turbo"] = "IJG, BSD-3-Clause and Zlib (its LICENSE.md).",
    ["libb2"] = "CC0-1.0 (its LICENSE).",
    ["opusfile"] = "BSD-3-Clause (its LICENSE).",
    ["speex"] = "BSD-3-Clause (its LICENSE).",
    ["libmodplug"] = "Public domain (its LICENSE).",
    ["jbigkit"] = "GPL-2.0-or-later: its COPYING says version 2 or any later version.",
    ["libdatrie"] = "LGPL-2.1-or-later (its COPYING).",
    ["libgme"] = "LGPL-2.1-or-later (its license.txt).",
    ["twolame"] = "LGPL-2.1-or-later (its COPYING).",
    ["libplacebo"] = "LGPL-2.1-or-later (its LICENSE).",
    ["pango"] = "LGPL-2.0-or-later: its COPYING is the GNU Library General Public License, version 2.",
    ["flac"] = "libFLAC is BSD-3-Clause (its COPYING.Xiph); MSYS2's LGPL, GPL and FDL are for its tools and "
        .. "manual, under the LGPL-2.1-or-later, the GPL-2.0-or-later and the GFDL.",
    -- MSYS2's package installs no license text: the license as the project releases it.
    ["libvorbis"] = "BSD-3-Clause. " .. UPSTREAM .. " under the Xiph BSD license.",
    ["libx264"] = "GPL-2.0-or-later. " .. UPSTREAM .. " under the GPL-2.0-or-later.",
    ["x265"] = "GPL-2.0-or-later. " .. UPSTREAM .. " under the GPL-2.0-or-later.",
    ["xvidcore"] = "GPL-2.0-or-later. " .. UPSTREAM .. " under the GPL-2.0-or-later.",
    ["libmad"] = "GPL-2.0-or-later. " .. UPSTREAM .. " under the GPL-2.0-or-later.",
    ["lame"] = "LGPL-2.0-or-later. " .. UPSTREAM .. " under the LGPL-2.0-or-later.",
    ["gmp"] = "LGPL-3.0-or-later OR GPL-2.0-or-later. " .. UPSTREAM .. " under both, at the user's choice.",
    ["libcaca"] = "WTFPL. " .. UPSTREAM .. " under the WTFPL.",
    ["opencore-amr"] = "Apache-2.0. " .. UPSTREAM .. " under the Apache-2.0.",
    -- MSYS2 joins licenses offered as alternatives with AND.
    ["kddockwidgets"] = "GPL-2.0-only OR GPL-3.0-only (both texts in its license folder): used under the "
        .. "GPL-3.0-only.",
    ["qt6-base"] = "Qt offers each module under the LGPL-3.0-only, the GPL-2.0-only or the GPL-3.0-only, and "
        .. "is used under the GPL-3.0-only. MSYS2 lists every file's license: AFL-2.1 is the freedesktop.org "
        .. "MIME database, also under the GPL-2.0-or-later; the rest is third-party code Qt ships under "
        .. "licenses it documents as compatible (doc.qt.io, Licenses Used in Qt).",
    ["qt6-declarative"] = "LGPL-3.0-only, GPL-2.0-only or GPL-3.0-only: used under the GPL-3.0-only.",
    ["qt6-svg"] = "LGPL-3.0-only, GPL-2.0-only or GPL-3.0-only: used under the GPL-3.0-only.",
    ["qt6-websockets"] = "LGPL-3.0-only, GPL-2.0-only or GPL-3.0-only: used under the GPL-3.0-only.",
}
local SPDX_ID = "[A-Za-z0-9.+:%-]+"
local OPERATORS = { AND = true, OR = true, WITH = true }

--- The license identifiers an SPDX expression names, as a set, without the exceptions added WITH them.
--- pacman's prefixes go: "documentation:spdx:GPL-3.0-or-later", the license of a package's manual,
--- counts as GPL-3.0-or-later.
function licenses.licenses_of(expression)
    local words = {}
    for word in expression:gmatch(SPDX_ID) do
        words[#words + 1] = word
    end
    local names = {}
    for index, word in ipairs(words) do
        if not OPERATORS[word] and words[index - 1] ~= "WITH" then
            names[word:match("^.*spdx:(.*)$") or word] = true
        end
    end
    return names
end

--- The licenses of each alternative of an SPDX expression: AND binds tighter than OR, and pacman's
--- commas join like AND. An expression with parentheses counts as one alternative naming all of them.
function licenses.alternatives(expression)
    if expression:find("(", 1, true) then
        return { licenses.licenses_of(expression) }
    end
    local found = {}
    local at = 1
    while true do
        local first, last = expression:find("%s+OR%s+", at)
        if not first then
            break
        end
        found[#found + 1] = licenses.licenses_of(expression:sub(at, first - 1))
        at = last + 1
    end
    found[#found + 1] = licenses.licenses_of(expression:sub(at))
    return found
end

--- Whether one alternative of the expression names only allowed licenses.
function licenses.is_allowed(expression)
    for _, names in ipairs(licenses.alternatives(expression)) do
        local any, all = false, true
        for name in pairs(names) do
            any = true
            all = all and licenses.ALLOWED[name] == true
        end
        if any and all then
            return true
        end
    end
    return false
end

local function sorted_keys(set)
    local keys = {}
    for key in pairs(set) do
        keys[#keys + 1] = key
    end
    table.sort(keys)
    return keys
end

function licenses.main()
    -- Here rather than at the top, so the tests of the functions above need no LuaFileSystem.
    local msys2 = require("msys2_packages")
    local by_license, failing = {}, {}
    for _, package in ipairs(msys2.runtime().packages) do
        local names = licenses.licenses_of(package.license)
        if next(names) == nil then
            names = { ["(none stated)"] = true }
        end
        for name in pairs(names) do
            by_license[name] = by_license[name] or {}
            by_license[name][package.name] = true
        end
        if not licenses.EXCEPTIONS[package.name] and not licenses.is_allowed(package.license) then
            local license = package.license ~= "" and package.license or "(none stated)"
            failing[#failing + 1] = ("%s: %s"):format(package.name, license)
        end
    end
    print("Licenses a release ships, with their packages:")
    for _, name in ipairs(sorted_keys(by_license)) do
        print(("  %s: %s"):format(name, table.concat(sorted_keys(by_license[name]), ", ")))
    end
    if #failing > 0 then
        print("\nNot allowed with the GPL-3.0-or-later (add the license to ALLOWED, or the package to EXCEPTIONS):")
        for _, line in ipairs(failing) do
            print("  " .. line)
        end
        return 1
    end
    print("\nEvery license can be combined with the GPL-3.0-or-later.")
    return 0
end

if arg and arg[0] and arg[0]:match("[^/\\]*$") == "licenses.lua" then
    os.exit(licenses.main())
end

return licenses
