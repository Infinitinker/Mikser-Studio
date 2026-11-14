-- Reads and writes the text files the scripts in tools/ work with: UTF-8, with every line ending read
-- as "\n", and written with "\n" alone, so a checkout with Windows line endings reads the same.

local text = {}

local REPLACEMENT = "\u{FFFD}"

--- The second byte a UTF-8 sequence may have after its first byte, and how many bytes follow the first.
local function sequence(first)
    if first >= 0xC2 and first <= 0xDF then
        return 0x80, 0xBF, 1
    elseif first == 0xE0 then
        return 0xA0, 0xBF, 2
    elseif first == 0xED then
        return 0x80, 0x9F, 2
    elseif first >= 0xE1 and first <= 0xEF then
        return 0x80, 0xBF, 2
    elseif first == 0xF0 then
        return 0x90, 0xBF, 3
    elseif first == 0xF4 then
        return 0x80, 0x8F, 3
    elseif first >= 0xF1 and first <= 0xF3 then
        return 0x80, 0xBF, 3
    end
    return nil
end

--- The bytes with each piece that is not UTF-8 replaced by U+FFFD: a byte that cannot start a sequence,
--- or the start of a sequence that breaks off, as one piece.
local function replace_invalid(bytes)
    local pieces = {}
    local at = 1
    while at <= #bytes do
        local first = bytes:byte(at)
        if first < 0x80 then
            local stop = bytes:find("[\128-\255]", at) or #bytes + 1
            pieces[#pieces + 1] = bytes:sub(at, stop - 1)
            at = stop
        else
            local low, high, following = sequence(first)
            local stop = at + 1
            local read = 0
            while low and read < following do
                local byte = bytes:byte(stop)
                if not byte or byte < low or byte > high then
                    break
                end
                stop, read, low, high = stop + 1, read + 1, 0x80, 0xBF
            end
            pieces[#pieces + 1] = (following and read == following) and bytes:sub(at, stop - 1) or REPLACEMENT
            at = stop
        end
    end
    return table.concat(pieces)
end

--- A text file, with "\r\n" and "\r" read as "\n". errors is "strict", the default, which stops at bytes
--- that are not UTF-8, or "replace", which reads them as U+FFFD.
function text.read(path, errors)
    local file = assert(io.open(path, "rb"))
    local bytes = file:read("a")
    file:close()
    if not utf8.len(bytes) then
        if errors ~= "replace" then
            error(path .. " is not UTF-8.", 0)
        end
        bytes = replace_invalid(bytes)
    end
    return (bytes:gsub("\r\n?", "\n"))
end

--- Writes a text file as it is, with "\n" line endings.
function text.write(path, contents)
    local file = assert(io.open(path, "wb"))
    file:write(contents)
    file:close()
end

--- The lines of a text, split at line ends, and at the vertical tab, form feed, file, group and
--- record separators, NEL and the line and paragraph separators. A line end at the end makes no empty
--- last line.
function text.lines(contents)
    local separated = contents
        :gsub("\r\n", "\n")
        :gsub("[\r\v\f\28\29\30]", "\n")
        :gsub("\u{85}", "\n")
        :gsub("\u{2028}", "\n")
        :gsub("\u{2029}", "\n")
    local lines = {}
    for line in separated:gmatch("([^\n]*)\n") do
        lines[#lines + 1] = line
    end
    local last = separated:match("[^\n]+$")
    if last then
        lines[#lines + 1] = last
    end
    return lines
end

return text
