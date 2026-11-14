-- The files and folders the scripts in tools/ read and write, through LuaFileSystem (lfs). Paths are
-- strings with "/" between their parts, which Windows takes too, and which the scripts' messages and
-- the programs they run get as well.

local lfs = require("lfs")
local process = require("process")

local files = {}

--- Two paths joined with one "/".
function files.join(folder, name)
    if folder == "" or folder == "." then
        return name
    end
    return (folder:gsub("[/\\]+$", "")) .. "/" .. name
end

--- The last part of a path.
function files.name(path)
    return path:match("([^/\\]*)[/\\]*$")
end

--- The folder a path is in.
function files.parent(path)
    return path:match("^(.*)[/\\][^/\\]*$") or "."
end

--- The extension of a path's last part, with its dot: "" when the part has none, or only a leading dot.
function files.suffix(path)
    local name = files.name(path)
    local dot = name:match("^.+()%.[^.]*$")
    return dot and name:sub(dot) or ""
end

function files.is_file(path)
    return lfs.attributes(path, "mode") == "file"
end

--- The size of a file, in bytes.
function files.size(path)
    return assert(lfs.attributes(path, "size"))
end

function files.is_dir(path)
    return lfs.attributes(path, "mode") == "directory"
end

local function parts(path)
    local found = {}
    for part in path:gmatch("[^/\\]+") do
        found[#found + 1] = process.windows and part:lower() or part
    end
    return found
end

--- Whether path a sorts before path b, part by part and, on Windows, without regard to case, as Python
--- sorts paths.
function files.before(a, b)
    local left, right = parts(a), parts(b)
    for index = 1, math.min(#left, #right) do
        if left[index] ~= right[index] then
            return left[index] < right[index]
        end
    end
    return #left < #right
end

--- The names in a folder, without "." and "..", sorted as paths are.
function files.list(folder)
    local names = {}
    for name in lfs.dir(folder) do
        if name ~= "." and name ~= ".." then
            names[#names + 1] = name
        end
    end
    table.sort(names, files.before)
    return names
end

--- Every file under a folder, at any depth, sorted as paths are.
function files.walk(folder)
    local found = {}
    local function visit(current)
        for _, name in ipairs(files.list(current)) do
            local path = files.join(current, name)
            local mode = lfs.attributes(path, "mode")
            if mode == "directory" then
                visit(path)
            elseif mode == "file" then
                found[#found + 1] = path
            end
        end
    end
    if files.is_dir(folder) then
        visit(folder)
    end
    table.sort(found, files.before)
    return found
end

--- The whole of a file, as bytes.
function files.read(path)
    local file = assert(io.open(path, "rb"))
    local bytes = file:read("a")
    file:close()
    return bytes
end

--- Makes a folder and the folders it is in.
function files.make_folders(path)
    if path == "" or path == "." or files.is_dir(path) then
        return
    end
    files.make_folders(files.parent(path))
    if not files.is_dir(path) then
        assert(lfs.mkdir(path))
    end
end

--- Removes a folder with everything in it; nothing when there is none.
function files.remove_tree(path)
    if not files.is_dir(path) then
        return
    end
    for _, name in ipairs(files.list(path)) do
        local child = files.join(path, name)
        if lfs.symlinkattributes(child, "mode") == "directory" then
            files.remove_tree(child)
        else
            assert(os.remove(child))
        end
    end
    assert(lfs.rmdir(path))
end

--- Copies a file, with its modification time.
function files.copy(source, target)
    local input = assert(io.open(source, "rb"))
    local output = assert(io.open(target, "wb"))
    while true do
        local block = input:read(1024 * 1024)
        if not block then
            break
        end
        output:write(block)
    end
    input:close()
    output:close()
    local attributes = assert(lfs.attributes(source))
    assert(lfs.touch(target, attributes.access, attributes.modification))
end

--- The absolute form of a path, with "/" between its parts.
function files.absolute(path)
    if not (path:match("^%a:[/\\]") or path:match("^[/\\]")) then
        path = files.join(assert(lfs.currentdir()), path)
    end
    return (path:gsub("\\", "/"))
end

return files
