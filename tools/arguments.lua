-- The command-line options of the scripts in tools/: flags alone, each with its help line. A flag may be
-- shortened while it stays unambiguous (--ch for --check), and -h or --help prints the usage.

local arguments = {}

local function usage(program, flags)
    local words = { "usage: " .. program, "[-h]" }
    for _, flag in ipairs(flags) do
        words[#words + 1] = "[" .. flag.name .. "]"
    end
    return table.concat(words, " ")
end

local function help(program, description, flags)
    local rows = { { "-h, --help", "show this help message and exit" } }
    for _, flag in ipairs(flags) do
        rows[#rows + 1] = { flag.name, flag.help }
    end
    local width = 0
    for _, row in ipairs(rows) do
        width = math.max(width, #row[1])
    end
    local lines = { usage(program, flags), "", description, "", "options:" }
    for _, row in ipairs(rows) do
        lines[#lines + 1] = ("  %-" .. width .. "s  %s"):format(row[1], row[2])
    end
    return table.concat(lines, "\n")
end

local function stop(program, flags, message)
    io.stderr:write(usage(program, flags), "\n", program, ": error: ", message, "\n")
    os.exit(2)
end

--- The flags given, as a set of their names without the dashes ({ check = true }). flags lists
--- { name = "--check", help = "..." }; the program's name is the script's file name. -h and --help
--- print the usage and exit with 0; anything else exits with 2.
function arguments.parse(given, description, flags)
    local program = (arg and arg[0] or "script"):match("[^/\\]*$")
    local all = { { name = "--help" } }
    for _, flag in ipairs(flags) do
        all[#all + 1] = flag
    end
    local found, unknown = {}, {}
    for _, word in ipairs(given) do
        local matches = {}
        if word == "-h" then
            matches = { all[1] }
        elseif word:match("^%-%-.") then
            for _, flag in ipairs(all) do
                if flag.name == word then
                    matches = { flag }
                    break
                elseif flag.name:sub(1, #word) == word then
                    matches[#matches + 1] = flag
                end
            end
        end
        if #matches > 1 then
            local names = {}
            for index, flag in ipairs(matches) do
                names[index] = flag.name
            end
            stop(program, flags, ("ambiguous option: %s could match %s"):format(word, table.concat(names, ", ")))
        elseif #matches == 0 then
            unknown[#unknown + 1] = word
        elseif matches[1].name == "--help" then
            print(help(program, description, flags))
            os.exit(0)
        else
            found[matches[1].name:sub(3)] = true
        end
    end
    if #unknown > 0 then
        stop(program, flags, "unrecognized arguments: " .. table.concat(unknown, " "))
    end
    return found
end

return arguments
