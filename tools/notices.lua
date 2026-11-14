-- Writes THIRD-PARTY-NOTICES.md, the license notices of everything a Windows release of Mikser Studio
-- ships from MSYS2: the libraries the program and the copied MLT and Qt files need, grouped by the
-- package they come from (tools/msys2_packages.lua). A release ships the file as THIRD-PARTY-NOTICES.txt.
--
-- Each package gets the license texts it installs. A package that installs none gets the standard text
-- of each license its SPDX expression names, as other packages install it, unless that text is a
-- template waiting for the project's own copyright line (BSD, MIT); one that has neither must be listed
-- in NO_TEXT, with the reason, until its text is added to tools/licenses/<package>.txt. Versions are
-- left out: MSYS2 moves on continually, while the texts rarely change.
--
-- Run it from the repository root, with MSYS2's packages installed as docs/development.md describes,
-- and commit the file whenever the libraries a release ships change:
--     lua5.4 tools/notices.lua           write THIRD-PARTY-NOTICES.md
--     lua5.4 tools/notices.lua --check   fail unless it is up to date

package.path = debug.getinfo(1, "S").source:match("^@(.-)[^/\\]*$") .. "?.lua;" .. package.path
local arguments = require("arguments")
local files = require("files")
local msys2 = require("msys2_packages")
local text = require("text")

local notices = {}

notices.NOTICES = "THIRD-PARTY-NOTICES.md"
local SHIPPED_AS = "THIRD-PARTY-NOTICES.txt"
--- License texts that MSYS2's packages don't install, as <package>.txt, taken from each project's
--- source.
local KEPT_LICENSES = "tools/licenses"
--- TODO: packages whose license text is still to be added to tools/licenses: their license needs the
--- project's own copyright notice, or MSYS2 names it in a way that doesn't say which text applies. Each
--- is listed in the notices with its source, so the gap shows; a package that installs a text or gets
--- one in tools/licenses must leave this list.
local NO_TEXT = {
    ["gmp"] = 'LGPL-3.0-or-later or GPL-2.0-or-later ("LGPL3 AND GPL" in MSYS2\'s metadata); no text installed.',
    ["icu"] = "ICU, whose text carries ICU's own copyright notice; MSYS2's package installs none.",
    ["lame"] = "LGPL-2.0-or-later (\"LGPL\" in MSYS2's metadata); MSYS2's package installs no text.",
    ["libass"] = "ISC, whose text carries libass's own copyright notice; MSYS2's package installs none.",
    ["libcaca"] = "WTFPL and LGPL (\"LGPL\" in MSYS2's metadata); MSYS2's package installs no text.",
    ["libmad"] = "GPL-2.0-or-later (\"GPL\" in MSYS2's metadata); MSYS2's package installs no text.",
    ["libtheora"] = "BSD-3-Clause, whose text carries Xiph's copyright notice; MSYS2's package installs none.",
    ["libvorbis"] = "BSD-3-Clause (custom), whose text carries Xiph's copyright notice; MSYS2's package "
        .. "installs none.",
    ["libvpx"] = "BSD-3-Clause, whose text carries the WebM project's copyright notice; MSYS2's package "
        .. "installs none.",
    ["libx264"] = "GPL-2.0-or-later (custom in MSYS2's metadata); MSYS2's package installs no text.",
    ["opencore-amr"] = "Apache-2.0 (\"Apache\" in MSYS2's metadata), with its NOTICE; MSYS2's package installs "
        .. "no text.",
    ["p11-kit"] = "BSD-3-Clause, whose text carries p11-kit's own copyright notice; MSYS2's package installs none.",
    ["x265"] = "GPL-2.0-or-later (\"GPL\" in MSYS2's metadata); MSYS2's package installs no text.",
    ["xvidcore"] = "GPL-2.0-or-later (\"GPL\" in MSYS2's metadata); MSYS2's package installs no text.",
}
local SPDX_ID = "[A-Za-z0-9.+%-]+"
local NOT_LICENSES = { AND = true, OR = true, WITH = true, custom = true, documentation = true, spdx = true }
--- The templates' placeholders, such as BSD's "Copyright (c) <year> <owner>.", which only the package's
--- own text can fill in.
local PLACEHOLDERS = { "<year>", "<owner>", "<copyright holder>", "<copyright holders>", "<name of author>" }

local HEADER = ([[
# Third-party notices

Mikser Studio's Windows release ships with the third-party libraries listed here, which it takes from
[MSYS2](https://www.msys2.org)'s UCRT64 packages: the C++ run time, Qt, KDDockWidgets, MLT, FFmpeg and
the libraries they need. Their licenses require these notices to come with every copy. Each library's
source is at its project's address below, and MSYS2's build of it at
https://github.com/msys2/MINGW-packages.

This file is written by `lua5.4 tools/notices.lua` from the packages a release is built with; do not
edit it by hand. A release ships it as %s.
]]):format(SHIPPED_AS)

local function stem(path)
    local name = files.name(path)
    local suffix = files.suffix(path)
    return name:sub(1, #name - #suffix)
end

--- The standard license texts that MSYS2's packages install as <SPDX id>.txt, by SPDX id.
local function spdx_texts(packages, root)
    local texts = {}
    for _, package in ipairs(packages) do
        for _, file in ipairs(msys2.license_files(package)) do
            if files.suffix(file) == ".txt" and files.name(files.parent(files.parent(file))) == "licenses" then
                texts[stem(file)] = texts[stem(file)] or files.join(root, file)
            end
        end
    end
    return texts
end

local function without_suffix(value, suffix)
    return value:sub(-#suffix) == suffix and value:sub(1, #value - #suffix) or value
end

--- The text of a license; GPL-3.0-or-later and GPL-3.0-only share one.
local function standard_text(license_id, texts)
    local base = without_suffix(without_suffix(without_suffix(license_id, "-or-later"), "-only"), "+")
    for _, candidate in ipairs({ license_id, base .. "-only", base .. "-or-later", base }) do
        if texts[candidate] then
            return texts[candidate]
        end
    end
    return nil
end

--- Whether a standard text opens with a copyright line still to be filled in: one of its first five
--- lines starts with "copyright" and holds a placeholder after it, whatever their case.
local function is_template(path)
    local lines = text.lines(text.read(path, "replace"))
    for index = 1, math.min(5, #lines) do
        local line = lines[index]:lower()
        local after = select(2, line:find("^%s*copyright"))
        if after then
            for _, placeholder in ipairs(PLACEHOLDERS) do
                if line:find(placeholder, after + 1, true) then
                    return true
                end
            end
        end
    end
    return false
end

--- The section of one package, and whether it has every text it needs. A text printed before, as Qt's
--- modules share theirs, is named rather than printed again; printed maps each text to where.
local function notice(package, root, texts, printed)
    local license = package.license ~= "" and package.license or "license not stated"
    local lines = { ("## %s (%s)"):format(package.name, license), "" }
    local function add(...)
        for _, line in ipairs({ ... }) do
            lines[#lines + 1] = line
        end
    end
    if package.url ~= "" then
        add("Source: " .. package.url, "")
    end
    local found = {}
    for _, file in ipairs(msys2.license_files(package)) do
        found[#found + 1] = { title = file:match("([^/]+/[^/]+)$") or file, path = files.join(root, file) }
    end
    local kept = ("%s/%s.txt"):format(KEPT_LICENSES, package.name)
    if files.is_file(kept) then
        found[#found + 1] = { title = kept, path = kept }
    end
    local complete = #found > 0
    if #found == 0 then
        local ids = {}
        for id in package.license:gmatch(SPDX_ID) do
            if not NOT_LICENSES[id] and not id:find("-exception", 1, true) then
                ids[#ids + 1] = id
            end
        end
        for _, id in ipairs(ids) do
            local path = standard_text(id, texts)
            if path and not is_template(path) then
                found[#found + 1] = { title = "the standard text of " .. id, path = path }
            end
        end
        complete = #ids > 0 and #found == #ids
    end
    if not complete then
        add("Its license text is still to be added: " .. (NO_TEXT[package.name] or "no text was found."), "")
    end
    for _, file in ipairs(found) do
        local contents = text.read(file.path, "replace"):match("^\n*(.-)\n*$")
        if printed[contents] then
            add("### " .. file.title, "", ("The same text as %s."):format(printed[contents]), "")
        else
            printed[contents] = ("%s under %s"):format(file.title, package.name)
            add("### " .. file.title, "", "````text", contents, "````", "")
        end
    end
    return table.concat(lines, "\n"), complete
end

--- The notices, as THIRD-PARTY-NOTICES.md holds them.
function notices.render()
    local root = msys2.root()
    local packages = msys2.runtime().packages
    local texts = spdx_texts(msys2.installed(root), root)
    local sections, unexplained, shipped, printed = {}, {}, {}, {}
    for _, package in ipairs(packages) do
        local section, complete = notice(package, root, texts, printed)
        sections[#sections + 1] = section
        shipped[package.name] = true
        if not complete and not NO_TEXT[package.name] then
            unexplained[#unexplained + 1] = package.name
        end
    end
    local stale = {}
    for name in pairs(NO_TEXT) do
        if not shipped[name] then
            stale[#stale + 1] = name
        end
    end
    table.sort(stale)
    local problems = {}
    if #unexplained > 0 then
        problems[#problems + 1] = ("no license text found for %s: add it to %s"):format(
            table.concat(unexplained, ", "),
            KEPT_LICENSES
        )
    end
    if #stale > 0 then
        problems[#problems + 1] = "NO_TEXT lists packages a release no longer ships: " .. table.concat(stale, ", ")
    end
    if #problems > 0 then
        msys2.fail("tools/notices.lua: " .. table.concat(problems, "; ") .. ".")
    end
    return HEADER .. "\n" .. table.concat(sections, "\n")
end

function notices.main()
    local given = arguments.parse(arg, "Write THIRD-PARTY-NOTICES.md.", {
        { name = "--check", help = "fail unless THIRD-PARTY-NOTICES.md is up to date" },
    })
    local rendered = notices.render()
    if given.check then
        local current = files.is_file(notices.NOTICES) and text.read(notices.NOTICES) or ""
        if current ~= rendered then
            print(("%s is out of date: run lua5.4 tools/notices.lua and commit it."):format(notices.NOTICES))
            return 1
        end
        print(("%s is up to date."):format(notices.NOTICES))
        return 0
    end
    text.write(notices.NOTICES, rendered)
    print(("Wrote %s."):format(notices.NOTICES))
    return 0
end

if arg and arg[0] and arg[0]:match("[^/\\]*$") == "notices.lua" then
    os.exit(notices.main())
end

return notices
