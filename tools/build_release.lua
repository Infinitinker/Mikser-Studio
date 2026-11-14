-- Builds the Windows release files of Mikser Studio from the package build (cmake --preset package, then
-- cmake --build --preset package) into build/dist:
--
-- - MikserStudio-<version>-windows-x64.zip: the program and the MCP bridge with everything they need
--   from MSYS2 (tools/msys2_packages.lua), LICENSE.txt and THIRD-PARTY-NOTICES.txt, unpacked and run
--   anywhere; CMake 4.3 or newer writes it;
-- - MikserStudio-<version>-windows-x64-setup.exe: the same files in an installer (tools/installer.iss),
--   built with Inno Setup 7's ISCC.exe, found through ISCC or in Program Files;
-- - MikserStudio-<version>-third-party-notices.md: THIRD-PARTY-NOTICES.md, to read without the app.
--
-- It fails unless THIRD-PARTY-NOTICES.md covers exactly what ships: when the program, the MCP bridge or a
-- bundled plugin imports an MSYS2 library that PROGRAM_LIBRARIES in tools/msys2_packages.lua does not
-- list, when what it copies is not what tools/notices.lua walks, or when the committed notices are not
-- what tools/notices.lua writes from this MSYS2.
--
-- Run it from the repository root, with MSYS2's packages installed as docs/development.md describes:
--     lua5.4 tools/build_release.lua              build the release files
--     lua5.4 tools/build_release.lua --version    print the version, the one way the workflows read it
-- --version needs Lua alone, without LuaFileSystem, so it also runs on a Linux runner.

package.path = debug.getinfo(1, "S").source:match("^@(.-)[^/\\]*$") .. "?.lua;" .. package.path
local arguments = require("arguments")
local process = require("process")
local text = require("text")
-- Required once --version is handled, which needs Lua alone: they need LuaFileSystem.
local files, msys2, notices

local release = {}

--- The product's name without spaces, as release files are named.
local NAME = "MikserStudio"
local BUILD = "build/package"
local CONFIGURE = "run cmake --preset package"
local STAGE_PARENT = "build/stage"
local STAGE = STAGE_PARENT .. "/" .. NAME
local DIST = "build/dist"
local PROGRAM = STAGE .. "/bin/mikser-studio.exe"
--- The MCP bridge (docs/api.md), a program of its own that ships next to the app.
local MCP_BRIDGE = STAGE .. "/bin/mikser-studio-mcp.exe"
--- Where Qt finds its plugins and QML modules in a release, relative to bin: MSYS2's Qt would look for
--- them under share/qt6, as it is installed there.
local QT_CONF = "[Paths]\nPrefix = .\nPlugins = .\nQmlImports = qml\n"

local function fail(message)
    io.stderr:write(message, "\n")
    os.exit(1)
end

--- The version in CMakeLists.txt's project().
function release.version()
    local found = text.read("CMakeLists.txt"):match("project%(MikserStudio%s+VERSION%s+(%d+%.%d+%.%d+)")
    if not found then
        fail("CMakeLists.txt's project() has no VERSION.")
    end
    return found
end

--- The program, the MCP bridge and the bundled plugins, as CMake installs them.
local function install()
    if not files.is_file(BUILD .. "/build.ninja") then
        fail(("No package build in %s: %s, then cmake --build --preset package."):format(BUILD, CONFIGURE))
    end
    -- Without it the release would run as Mikser Studio Dev (src/core/include/mikserstudio/core/Instance.h).
    if not text.read(BUILD .. "/CMakeCache.txt"):find("MIKSERSTUDIO_INSTALLED_APP:BOOL=ON", 1, true) then
        fail(("The build in %s would run as Mikser Studio Dev: %s, then build it again."):format(BUILD, CONFIGURE))
    end
    files.remove_tree(STAGE_PARENT)
    process.run({ "cmake", "--install", BUILD, "--prefix", STAGE })
end

local function names(packages)
    local found = {}
    for index, package in ipairs(packages) do
        found[index] = package.name
    end
    return table.concat(found, "\n")
end

--- The libraries the program and plugins need, once THIRD-PARTY-NOTICES.md is known to cover them:
--- every MSYS2 library they import is in PROGRAM_LIBRARIES, so tools/notices.lua, which walks from that
--- list without a build, walks to the same libraries; and the committed notices are what it writes now.
local function check_notices(programs, root)
    local available, listed = {}, {}
    for _, name in ipairs(files.list(files.join(root, msys2.ENVIRONMENT .. "/bin"))) do
        if name:lower():sub(-4) == ".dll" then
            available[name:lower()] = true
        end
    end
    for _, name in ipairs(msys2.PROGRAM_LIBRARIES) do
        listed[name:lower()] = true
    end
    for _, program in ipairs(programs) do
        local unlisted = {}
        for _, name in ipairs(msys2.imports(program)) do
            if available[name:lower()] and not listed[name:lower()] then
                unlisted[#unlisted + 1] = name
            end
        end
        table.sort(unlisted)
        if #unlisted > 0 then
            fail(
                ("%s imports %s, which PROGRAM_LIBRARIES in tools/msys2_packages.lua does not list: add them "):format(
                    files.name(program),
                    table.concat(unlisted, ", ")
                ) .. "there, then run lua5.4 tools/notices.lua and commit THIRD-PARTY-NOTICES.md."
            )
        end
    end
    local shipped = msys2.runtime(programs, root)
    if names(shipped.packages) ~= names(msys2.runtime(nil, root).packages) then
        fail("What the release copies differs from what tools/notices.lua walks: report this as a bug.")
    end
    if text.read(notices.NOTICES) ~= notices.render() then
        fail(
            ("%s does not match this MSYS2's packages: run lua5.4 tools/notices.lua and commit %s."):format(
                notices.NOTICES,
                notices.NOTICES
            )
        )
    end
    return shipped.libraries
end

--- The libraries from MSYS2 the program, the MCP bridge and the plugins need, and MLT's and Qt's files.
local function add_runtime()
    local root = msys2.root()
    local programs = { PROGRAM, MCP_BRIDGE }
    for _, path in ipairs(files.walk(STAGE .. "/bin/plugins")) do
        if path:lower():sub(-4) == ".dll" then
            programs[#programs + 1] = path
        end
    end
    for _, library in ipairs(check_notices(programs, root)) do
        files.copy(library, STAGE .. "/bin/" .. files.name(library))
    end
    for _, file in ipairs(msys2.copied_files(files.join(root, msys2.ENVIRONMENT))) do
        local target = STAGE .. "/" .. file.destination
        files.make_folders(files.parent(target))
        files.copy(file.source, target)
    end
    -- In text mode, as the other files of a Windows release: with "\r\n" on Windows.
    local conf = assert(io.open(STAGE .. "/bin/qt.conf", "w"))
    conf:write(QT_CONF)
    conf:close()
end

--- Mikser Studio's license and the notices, under the names a release ships them with.
local function add_texts()
    files.copy("LICENSE", STAGE .. "/LICENSE.txt")
    files.copy("THIRD-PARTY-NOTICES.md", STAGE .. "/THIRD-PARTY-NOTICES.txt")
end

--- The zip of the staged files, each under MikserStudio/, in the order Python's zipfile wrote them;
--- CMake's tar writes it, with deflate at level 9.
local function make_zip(name)
    local path = ("%s/%s-windows-x64.zip"):format(DIST, name)
    local list = STAGE_PARENT .. "/zip-files.txt"
    local entries = {}
    for index, file in ipairs(files.walk(STAGE)) do
        entries[index] = NAME .. file:sub(#STAGE + 1)
    end
    text.write(list, table.concat(entries, "\n") .. "\n")
    process.run({
        "cmake",
        "-E",
        "chdir",
        STAGE_PARENT,
        "cmake",
        "-E",
        "tar",
        "cf",
        files.absolute(path),
        "--format=zip",
        "--cmake-tar-compression-method=deflate",
        "--cmake-tar-compression-level=9",
        "--files-from=" .. files.absolute(list),
    })
    os.remove(list)
    return path
end

local function make_notices(name)
    local path = ("%s/%s-third-party-notices.md"):format(DIST, name)
    files.copy("THIRD-PARTY-NOTICES.md", path)
    return path
end

local function make_installer(version)
    local program_files = (os.getenv("ProgramFiles") or "C:/Program Files"):gsub("\\", "/")
    local iscc = os.getenv("ISCC") or (program_files .. "/Inno Setup 7/ISCC.exe")
    if not files.is_file(iscc) then
        fail(("Inno Setup 7's ISCC.exe was not found at %s: install it, or set ISCC to where it is."):format(iscc))
    end
    process.run({
        iscc,
        "/Qp",
        "/DAppVersion=" .. version,
        "/DNumericVersion=" .. version .. ".0",
        "/DSourceDir=" .. files.absolute(STAGE),
        "/DOutputDir=" .. files.absolute(DIST),
        "tools/installer.iss",
    })
    return ("%s/%s-%s-windows-x64-setup.exe"):format(DIST, NAME, version)
end

function release.main()
    local given = arguments.parse(arg, "Build the Windows release files.", {
        { name = "--version", help = "print the version in CMakeLists.txt and stop" },
    })
    if given.version then
        print(release.version())
        return 0
    end
    files, msys2, notices = require("files"), require("msys2_packages"), require("notices")
    local version = release.version()
    install()
    add_runtime()
    add_texts()
    files.remove_tree(DIST)
    files.make_folders(DIST)
    local built = {
        make_zip(NAME .. "-" .. version),
        make_installer(version),
        make_notices(NAME .. "-" .. version),
    }
    for _, path in ipairs(built) do
        print(("Built %s (%.1f MB)"):format(path, files.size(path) / 1e6))
    end
    return 0
end

if arg and arg[0] and arg[0]:match("[^/\\]*$") == "build_release.lua" then
    os.exit(release.main())
end

return release
