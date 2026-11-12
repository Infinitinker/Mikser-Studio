#include "FlatJson.h"

#include <cstdint>

namespace hello {

namespace {

class Reader {
public:
    explicit Reader(std::string_view text)
        : m_text(text)
    {
    }

    std::optional<std::map<std::string, std::string>> object()
    {
        std::map<std::string, std::string> members;
        skipSpace();
        if (!consume('{'))
            return std::nullopt;
        skipSpace();
        if (consume('}'))
            return finish(std::move(members));
        while (true) {
            skipSpace();
            auto key = string();
            skipSpace();
            if (!key || !consume(':'))
                return std::nullopt;
            skipSpace();
            if (peek() == '"') {
                auto value = string();
                if (!value)
                    return std::nullopt;
                members.insert_or_assign(std::move(*key), std::move(*value));
            } else if (!skipValue()) {
                return std::nullopt;
            }
            skipSpace();
            if (consume('}'))
                return finish(std::move(members));
            if (!consume(','))
                return std::nullopt;
        }
    }

private:
    std::optional<std::map<std::string, std::string>> finish(std::map<std::string, std::string> members)
    {
        skipSpace();
        if (m_pos != m_text.size())
            return std::nullopt;
        return members;
    }

    [[nodiscard]] char peek() const { return m_pos < m_text.size() ? m_text[m_pos] : '\0'; }

    bool consume(char expected)
    {
        if (peek() != expected)
            return false;
        ++m_pos;
        return true;
    }

    void skipSpace()
    {
        while (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r')
            ++m_pos;
    }

    std::optional<std::uint32_t> hex4()
    {
        if (m_pos + 4 > m_text.size())
            return std::nullopt;
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = m_text[m_pos++];
            value <<= 4;
            if (c >= '0' && c <= '9')
                value |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                value |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                value |= static_cast<std::uint32_t>(c - 'A' + 10);
            else
                return std::nullopt;
        }
        return value;
    }

    static void appendUtf8(std::string& out, std::uint32_t cp)
    {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    std::optional<std::uint32_t> escapedCodePoint()
    {
        auto unit = hex4();
        if (!unit)
            return std::nullopt;
        if (*unit < 0xD800 || *unit > 0xDFFF)
            return unit;
        if (*unit > 0xDBFF || !consume('\\') || !consume('u'))
            return std::nullopt;
        auto low = hex4();
        if (!low || *low < 0xDC00 || *low > 0xDFFF)
            return std::nullopt;
        return 0x10000 + ((*unit - 0xD800) << 10) + (*low - 0xDC00);
    }

    std::optional<std::string> string()
    {
        if (!consume('"'))
            return std::nullopt;
        std::string out;
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos++];
            if (c == '"')
                return out;
            if (static_cast<unsigned char>(c) < 0x20)
                return std::nullopt;
            if (c != '\\') {
                out += c;
                continue;
            }
            const char escape = peek();
            ++m_pos;
            switch (escape) {
            case '"':
                out += '"';
                break;
            case '\\':
                out += '\\';
                break;
            case '/':
                out += '/';
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u': {
                auto cp = escapedCodePoint();
                if (!cp)
                    return std::nullopt;
                appendUtf8(out, *cp);
                break;
            }
            default:
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    // Skips a non-string value: a nested object/array (tracking strings inside it) or a literal.
    bool skipValue()
    {
        if (peek() != '{' && peek() != '[') {
            const std::size_t start = m_pos;
            while (m_pos < m_text.size() && m_text[m_pos] != ',' && m_text[m_pos] != '}' && m_text[m_pos] != ' ' &&
                   m_text[m_pos] != '\n' && m_text[m_pos] != '\r' && m_text[m_pos] != '\t')
                ++m_pos;
            return m_pos > start;
        }
        int depth = 0;
        while (m_pos < m_text.size()) {
            const char c = peek();
            if (c == '"') {
                if (!string())
                    return false;
                continue;
            }
            ++m_pos;
            if (c == '{' || c == '[')
                ++depth;
            else if ((c == '}' || c == ']') && --depth == 0)
                return true;
        }
        return false;
    }

    std::string_view m_text;
    std::size_t m_pos = 0;
};

}  // namespace

std::optional<std::map<std::string, std::string>> stringMembers(std::string_view json)
{
    return Reader(json).object();
}

std::string quoted(std::string_view text)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out = "\"";
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (byte < 0x20) {
                out += "\\u00";
                out += kHex[byte >> 4];
                out += kHex[byte & 0x0F];
            } else {
                out += c;
            }
        }
    }
    out += '"';
    return out;
}

}  // namespace hello
