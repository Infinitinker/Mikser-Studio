-- What a Windows release of Mikser Studio takes from MSYS2: the run-time files it copies (COPIED: MLT's
-- modules and data, Qt's plugins and QML modules) and every library they and the program need, followed
-- through the libraries' imports, with the packages they come from and those packages' licenses, read
-- from MSYS2's own package database. tools/build_release.lua copies these files, tools/licenses.lua
-- checks their licenses and tools/notices.lua writes their notices.
--
-- MSYS2 is found where MSYS2_ROOT points, else where cygpath says in an MSYS2 shell, else in C:/msys64.

package.path = debug.getinfo(1, "S").source:match("^@(.-)[^/\\]*$") .. "?.lua;" .. package.path
local files = require("files")
local process = require("process")
local text = require("text")

local msys2 = {}

local INSTALL = "docs/development.md lists them"
--- The UCRT64 environment's package names start with this.
msys2.PREFIX = "mingw-w64-ucrt-x86_64-"
msys2.ENVIRONMENT = "ucrt64"
--- The MLT modules a release ships (lib/mlt/libmlt<module>.dll): the services playback needs and the
--- optional ones docs/development.md installs, the audio outputs, and MLT's XML. Kdenlive's, frei0r's and
--- the other modules' services are not used.
msys2.MLT_MODULES = {
    "core",
    "avformat",
    "qt6",
    "plus",
    "resample",
    "sox",
    "rubberband",
    "rnnoise",
    "ladspa",
    "rtaudio",
    "sdl2",
    "xml",
}
--- The files of the UCRT64 environment a release copies besides the libraries, and the folder of the
--- release they go to, keeping the path under the pattern's fixed part: MLT's modules and data, where the
--- engine looks for them (lib/mlt and share/mlt next to bin), the Qt plugins a desktop app loads, and the
--- QML modules the panels import (QtQuick, with its Controls and Layouts, and QtQml), where Qt looks for
--- them next to the program. A pattern is a file, a folder's files (<folder>/*) or every file under a
--- folder (<folder>/**/*).
msys2.COPIED = {}
for _, module in ipairs(msys2.MLT_MODULES) do
    msys2.COPIED[#msys2.COPIED + 1] = { "lib/mlt/libmlt" .. module .. ".dll", "lib/mlt" }
end
for _, copied in ipairs({
    { "share/mlt/**/*", "share/mlt" },
    { "share/qt6/plugins/platforms/*", "bin/platforms" },
    { "share/qt6/plugins/styles/*", "bin/styles" },
    { "share/qt6/plugins/imageformats/*", "bin/imageformats" },
    { "share/qt6/plugins/iconengines/*", "bin/iconengines" },
    { "share/qt6/plugins/tls/*", "bin/tls" },
    { "share/qt6/plugins/networkinformation/*", "bin/networkinformation" },
    { "share/qt6/qml/QtQuick/**/*", "bin/qml/QtQuick" },
    { "share/qt6/qml/QtQml/**/*", "bin/qml/QtQml" },
}) do
    msys2.COPIED[#msys2.COPIED + 1] = copied
end
--- The MSYS2 libraries mikser-studio.exe imports. tools/build_release.lua fails when the program or the
--- MCP bridge imports one that is not listed, so the notices, which are
--- written without a build, never miss a library.
msys2.PROGRAM_LIBRARIES = {
    "Qt6Core.dll",
    "Qt6Gui.dll",
    "Qt6HttpServer.dll",
    "Qt6Network.dll",
    "Qt6Qml.dll",
    "Qt6QuickControls2.dll",
    "Qt6QuickWidgets.dll",
    "Qt6WebSockets.dll",
    "Qt6Widgets.dll",
    "libgcc_s_seh-1.dll",
    "libkddockwidgets-qt62.dll",
    "libmlt++-7.dll",
    "libmlt-7.dll",
    "libstdc++-6.dll",
    "libwinpthread-1.dll",
    "lua54.dll",
}
--- The MSYS2 packages compiled into the program, whose files a release doesn't copy but whose notices
--- it ships: sol2, the header-only C++ binding of the Lua host.
msys2.COMPILED_IN = {
    "sol2",
}
--- The MSYS2 libraries that copied files load by name while running, which their import tables don't
--- show: MSYS2's SDL2.dll (sdl2-compat, which MLT's sdl2 module imports) loads SDL3.dll, and Qt's OpenSSL
--- backend (tls/qopensslbackend.dll) loads libssl and libcrypto. Without SDL3.dll, MLT stops start-up
--- with an error box.
msys2.LOADED_LIBRARIES = {
    "SDL3.dll",
    "libcrypto-3-x64.dll",
    "libssl-3-x64.dll",
}

--- Stops the script with a message, as a failed step.
function msys2.fail(message)
    io.stderr:write(message, "\n")
    os.exit(1)
end

--- Whether a file of a package, relative to the MSYS2 root, is a license text: anything in
--- share/licenses, or a license file in a folder of share/doc.
function msys2.is_license_file(path)
    local lower = path:lower()
    local licensed = lower:match("^ucrt64/share/licenses/(.*)$")
    if licensed then
        return #licensed >= 2 and licensed:sub(-1) ~= "/"
    end
    local documented = lower:match("^ucrt64/share/doc/(.*)$")
    if not documented or not documented:find("/") then
        return false
    end
    local name = documented:match("[^/]*$")
    for _, start in ipairs({ "^licen[cs]e", "^copying", "^copyright", "^notice" }) do
        if name:find(start) then
            return true
        end
    end
    return false
end

--- The license texts a package installs, relative to the MSYS2 root.
function msys2.license_files(package)
    local found = {}
    for _, path in ipairs(package.files) do
        if msys2.is_license_file(path) then
            found[#found + 1] = path
        end
    end
    return found
end

--- MSYS2_ROOT; else, in an MSYS2 shell, the root cygpath gives (Git Bash has a cygpath too, whose root
--- holds no pacman database); else C:/msys64.
function msys2.root()
    local root = os.getenv("MSYS2_ROOT")
    if root and root ~= "" then
        return (root:gsub("\\", "/"))
    end
    local found = process.capture({ "cygpath", "-m", "/" }, true):match("^%s*(.-)%s*$")
    if found ~= "" and files.is_dir(files.join(found, "var/lib/pacman/local")) then
        return found
    end
    return "C:/msys64"
end

--- The fields of a pacman database file: %NAME% and the lines under it up to a blank one.
local function fields(contents)
    local found = {}
    local current
    for _, line in ipairs(text.lines(contents)) do
        if line:sub(1, 1) == "%" and line:sub(-1) == "%" then
            local name = line:match("^%%*(.-)%%*$")
            found[name] = found[name] or {}
            current = found[name]
        elseif line == "" then
            current = nil
        elseif current then
            current[#current + 1] = line
        end
    end
    return found
end

local function without_prefix(value, prefix)
    return value:sub(1, #prefix) == prefix and value:sub(#prefix + 1) or value
end

local installed_cache = {}

--- Every installed UCRT64 package: its name without the environment's prefix ("qt6-base"), its URL, its
--- license (its SPDX expression, or the names pacman gives when it has none) and its files, relative to
--- the MSYS2 root.
function msys2.installed(root)
    if installed_cache[root] then
        return installed_cache[root]
    end
    local database = files.join(root, "var/lib/pacman/local")
    if not files.is_dir(database) then
        msys2.fail(("MSYS2's package database was not found in %s: set MSYS2_ROOT to where MSYS2 is."):format(database))
    end
    local packages = {}
    for _, name in ipairs(files.list(database)) do
        if name:sub(1, #msys2.PREFIX) == msys2.PREFIX then
            local folder = files.join(database, name)
            local desc = fields(text.read(files.join(folder, "desc")))
            local package_files = {}
            for _, path in ipairs(fields(text.read(files.join(folder, "files"))).FILES or {}) do
                if path:sub(-1) ~= "/" then
                    package_files[#package_files + 1] = path
                end
            end
            packages[#packages + 1] = {
                name = without_prefix(desc.NAME[1], msys2.PREFIX),
                url = (desc.URL or {})[1] or "",
                license = without_prefix(table.concat(desc.LICENSE or {}, " AND "), "spdx:"),
                files = package_files,
            }
        end
    end
    installed_cache[root] = packages
    return packages
end

--- A little-endian unsigned number of size bytes, at a 0-based offset.
local function number(data, at, size)
    return (string.unpack("<I" .. size, data, at + 1))
end

--- The names of the DLLs a Windows program or library imports, read from its PE import table.
function msys2.imports(library)
    local data = files.read(library)
    local pe = number(data, 0x3C, 4)
    local sections_count = number(data, pe + 6, 2)
    local optional_size = number(data, pe + 20, 2)
    local optional = pe + 24
    local directories = optional + (number(data, optional, 2) == 0x20B and 112 or 96)
    local import_rva = number(data, directories + 8, 4)
    local headers = optional + optional_size
    local sections = {}
    for index = 0, sections_count - 1 do
        local at = headers + 40 * index + 8
        sections[#sections + 1] = {
            virtual_size = number(data, at, 4),
            address = number(data, at + 4, 4),
            raw_size = number(data, at + 8, 4),
            raw_offset = number(data, at + 12, 4),
        }
    end

    local function offset(rva)
        for _, section in ipairs(sections) do
            if section.address <= rva and rva < section.address + math.max(section.virtual_size, section.raw_size) then
                return rva - section.address + section.raw_offset
            end
        end
        error(library .. ": an address outside its sections", 0)
    end

    local names = {}
    if import_rva ~= 0 then
        local entry = offset(import_rva)
        while true do
            local name_rva = number(data, entry + 12, 4)
            if name_rva == 0 then
                break
            end
            local start = offset(name_rva)
            names[#names + 1] = data:sub(start + 1, data:find("\0", start + 1, true) - 1)
            entry = entry + 20
        end
    end
    return names
end

--- The files a COPIED pattern matches in the environment, relative to it, sorted as paths are.
local function matching(environment, pattern)
    local folder, depth = pattern:match("^(.*)/(%*%*/%*)$")
    if not folder then
        folder, depth = pattern:match("^(.*)/(%*)$")
    end
    if not folder then
        return files.is_file(files.join(environment, pattern)) and { pattern } or {}
    end
    local base = files.join(environment, folder)
    local found = {}
    if depth == "*" then
        if files.is_dir(base) then
            for _, name in ipairs(files.list(base)) do
                if files.is_file(files.join(base, name)) then
                    found[#found + 1] = folder .. "/" .. name
                end
            end
        end
    else
        for _, path in ipairs(files.walk(base)) do
            found[#found + 1] = folder .. path:sub(#base + 1)
        end
    end
    return found
end

--- The COPIED files of the environment, each as { source, relative to the environment, destination in
--- the release }.
function msys2.copied_files(environment)
    local copied = {}
    for _, entry in ipairs(msys2.COPIED) do
        local pattern, destination = entry[1], entry[2]
        local fixed = {}
        for part in files.parent(pattern):gmatch("[^/]+") do
            if not part:find("*", 1, true) then
                fixed[#fixed + 1] = part
            end
        end
        local fixed_folder = table.concat(fixed, "/")
        local found = matching(environment, pattern)
        if #found == 0 then
            msys2.fail(
                ("Nothing matches %s: install MSYS2's packages (%s)."):format(files.join(environment, pattern), INSTALL)
            )
        end
        for _, relative in ipairs(found) do
            local under = fixed_folder == "" and relative or relative:sub(#fixed_folder + 2)
            copied[#copied + 1] = {
                source = files.join(environment, relative),
                relative = relative,
                destination = destination .. "/" .. under,
            }
        end
    end
    return copied
end

local function is_program(path)
    local suffix = files.suffix(path):lower()
    return suffix == ".exe" or suffix == ".dll"
end

--- What a release takes from MSYS2: the libraries that the program (PROGRAM_LIBRARIES), the COPIED files
--- (with LOADED_LIBRARIES) and any other programs and libraries given need from the environment's bin
--- folder, followed through their imports and sorted as paths are, and the packages these files come
--- from, sorted by name. DLLs that are not in MSYS2's bin folder belong to Windows.
function msys2.runtime(programs, root)
    root = root or msys2.root()
    local environment = files.join(root, msys2.ENVIRONMENT)
    local owners = {}
    for _, package in ipairs(msys2.installed(root)) do
        for _, path in ipairs(package.files) do
            owners[path] = package
        end
    end
    local bin_folder = files.join(environment, "bin")
    local available = {}
    if files.is_dir(bin_folder) then
        for _, name in ipairs(files.list(bin_folder)) do
            if name:lower():sub(-4) == ".dll" then
                available[name:lower()] = name
            end
        end
    end

    local copied = msys2.copied_files(environment)
    local wanted = { table.unpack(msys2.PROGRAM_LIBRARIES) }
    for _, name in ipairs(msys2.LOADED_LIBRARIES) do
        wanted[#wanted + 1] = name
    end
    local missing = {}
    for _, name in ipairs(wanted) do
        if not available[name:lower()] then
            missing[#missing + 1] = name
        end
    end
    if #missing > 0 then
        msys2.fail(
            ("Not in %s: %s. Install MSYS2's packages (%s)."):format(bin_folder, table.concat(missing, ", "), INSTALL)
        )
    end
    local libraries, seen, waiting = {}, {}, {}
    for _, name in ipairs(wanted) do
        local library = available[name:lower()]
        libraries[#libraries + 1] = library
        seen[library] = true
        waiting[#waiting + 1] = files.join(bin_folder, library)
    end
    for _, path in ipairs(programs or {}) do
        if is_program(path) then
            waiting[#waiting + 1] = path
        end
    end
    for _, file in ipairs(copied) do
        if is_program(file.source) then
            waiting[#waiting + 1] = file.source
        end
    end
    while #waiting > 0 do
        local current = table.remove(waiting)
        for _, name in ipairs(msys2.imports(current)) do
            local library = available[name:lower()]
            if library and not seen[library] then
                seen[library] = true
                libraries[#libraries + 1] = library
                waiting[#waiting + 1] = files.join(bin_folder, library)
            end
        end
    end

    local used, packages = {}, {}
    local function use(relative)
        local package = owners[relative]
        if package and not used[package] then
            used[package] = true
            packages[#packages + 1] = package
        end
    end
    local paths = {}
    for index, library in ipairs(libraries) do
        use(msys2.ENVIRONMENT .. "/bin/" .. library)
        paths[index] = files.join(bin_folder, library)
    end
    for _, file in ipairs(copied) do
        use(msys2.ENVIRONMENT .. "/" .. file.relative)
    end
    local by_name = {}
    for _, package in pairs(owners) do
        by_name[package.name] = package
    end
    for _, name in ipairs(msys2.COMPILED_IN) do
        local package = by_name[name]
        if not package then
            msys2.fail(
                ("MSYS2's %s%s is not installed. Install MSYS2's packages (%s)."):format(msys2.PREFIX, name, INSTALL)
            )
        end
        if not used[package] then
            used[package] = true
            packages[#packages + 1] = package
        end
    end
    table.sort(paths, files.before)
    table.sort(packages, function(a, b)
        return a.name < b.name
    end)
    return { libraries = paths, packages = packages }
end

return msys2
