-- Runs the programs the scripts in tools/ need (git, cmake, cygpath, ISCC), from a list of arguments.
-- Lua starts a program through the system's shell: cmd.exe on Windows, also when the script runs in
-- Git Bash or an MSYS2 shell, and /bin/sh elsewhere. Every argument is quoted for it, so paths with
-- spaces and characters the shell would read pass through unchanged.

local process = {}

process.windows = package.config:sub(1, 1) == "\\"

--- One argument, quoted so that a Windows program's command-line parser reads it back unchanged:
--- backslashes are doubled only before a quote, which is escaped.
local function quote_windows(argument)
    local quoted = argument:gsub('(\\*)"', function(slashes)
        return slashes .. slashes .. '\\"'
    end)
    quoted = quoted:gsub("(\\+)$", "%1%1")
    return '"' .. quoted .. '"'
end

local function quote_posix(argument)
    return "'" .. argument:gsub("'", "'\\''") .. "'"
end

--- The command line of a list of arguments, for the system's shell. cmd.exe drops the first and the last
--- quote of a command line that starts with one, so the whole line goes in one more pair.
local function command_line(arguments, quiet)
    local quote = process.windows and quote_windows or quote_posix
    local words = {}
    for index, argument in ipairs(arguments) do
        words[index] = quote(argument)
    end
    local line = table.concat(words, " ")
    if quiet then
        line = line .. (process.windows and " 2>nul" or " 2>/dev/null")
    end
    return process.windows and '"' .. line .. '"' or line
end

--- Runs a program and returns its output, with its lines ending in "\n", and whether it exited with 0.
--- quiet drops what it writes to stderr.
function process.capture(arguments, quiet)
    local pipe = assert(io.popen(command_line(arguments, quiet), "r"))
    local output = pipe:read("a")
    local ok = pipe:close()
    return output, ok == true
end

--- Runs a program with the script's own output, and stops the script, as a failed step, when the
--- program fails.
function process.run(arguments)
    local ok, _, code = os.execute(command_line(arguments, false))
    if not ok then
        io.stderr:write(("%s failed with exit status %s.\n"):format(arguments[1], tostring(code)))
        os.exit(1)
    end
end

return process
