#include "Json.h"
#include "Util.h"
#include <charconv>

namespace pf
{
    namespace
    {
        const Json& NullJson()
        {
            static const Json null;
            return null;
        }

        const std::string& EmptyString()
        {
            static const std::string empty;
            return empty;
        }

        void AppendUtf8(std::string& out, uint32_t cp)
        {
            if (cp < 0x80)
            {
                out += static_cast<char>(cp);
            }
            else if (cp < 0x800)
            {
                out += static_cast<char>(0xC0 | (cp >> 6));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else if (cp < 0x10000)
            {
                out += static_cast<char>(0xE0 | (cp >> 12));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
            else
            {
                out += static_cast<char>(0xF0 | (cp >> 18));
                out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }
    }

    class JsonParser
    {
    public:
        explicit JsonParser(std::string_view text) : m_text(text) {}

        std::expected<Json, std::string> Run()
        {
            Json value;
            if (!ParseValue(value, 0))
                return std::unexpected(m_error);
            SkipWhitespace();
            if (m_pos != m_text.size())
                return std::unexpected(Error("unexpected trailing characters"));
            return value;
        }

    private:
        std::string Error(std::string_view message)
        {
            // Compute line/column for a helpful message.
            size_t line = 1, column = 1;
            for (size_t i = 0; i < m_pos && i < m_text.size(); ++i)
            {
                if (m_text[i] == '\n')
                {
                    ++line;
                    column = 1;
                }
                else
                {
                    ++column;
                }
            }
            m_error = std::format("JSON error at line {}, column {}: {}", line, column, message);
            return m_error;
        }

        void SkipWhitespace()
        {
            while (m_pos < m_text.size())
            {
                char c = m_text[m_pos];
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
                {
                    ++m_pos;
                }
                else if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '/')
                {
                    while (m_pos < m_text.size() && m_text[m_pos] != '\n')
                        ++m_pos;
                }
                else if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '*')
                {
                    m_pos += 2;
                    while (m_pos + 1 < m_text.size() && !(m_text[m_pos] == '*' && m_text[m_pos + 1] == '/'))
                        ++m_pos;
                    m_pos = std::min(m_pos + 2, m_text.size());
                }
                else
                {
                    break;
                }
            }
        }

        bool ParseValue(Json& out, int depth)
        {
            if (depth > 256)
            {
                Error("nesting too deep");
                return false;
            }
            SkipWhitespace();
            if (m_pos >= m_text.size())
            {
                Error("unexpected end of input");
                return false;
            }
            char c = m_text[m_pos];
            switch (c)
            {
            case '{': return ParseObject(out, depth);
            case '[': return ParseArray(out, depth);
            case '"':
                out.m_type = Json::Type::String;
                return ParseString(out.m_string);
            case 't': return ParseLiteral("true", out, Json(true));
            case 'f': return ParseLiteral("false", out, Json(false));
            case 'n': return ParseLiteral("null", out, Json());
            default:
                if (c == '-' || (c >= '0' && c <= '9'))
                    return ParseNumber(out);
                Error(std::format("unexpected character '{}'", c));
                return false;
            }
        }

        bool ParseLiteral(std::string_view literal, Json& out, Json value)
        {
            if (m_text.substr(m_pos, literal.size()) != literal)
            {
                Error("invalid literal");
                return false;
            }
            m_pos += literal.size();
            out = std::move(value);
            return true;
        }

        bool ParseNumber(Json& out)
        {
            size_t start = m_pos;
            if (m_text[m_pos] == '-')
                ++m_pos;
            while (m_pos < m_text.size())
            {
                char c = m_text[m_pos];
                if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')
                    ++m_pos;
                else
                    break;
            }
            double value = 0.0;
            const char* first = m_text.data() + start;
            const char* last = m_text.data() + m_pos;
            auto result = std::from_chars(first, last, value);
            if (result.ec != std::errc() || result.ptr != last)
            {
                Error("invalid number");
                return false;
            }
            out = Json(value);
            return true;
        }

        bool ParseHex4(uint32_t& value)
        {
            if (m_pos + 4 > m_text.size())
            {
                Error("truncated unicode escape");
                return false;
            }
            value = 0;
            for (int i = 0; i < 4; ++i)
            {
                char c = m_text[m_pos++];
                value <<= 4;
                if (c >= '0' && c <= '9') value |= static_cast<uint32_t>(c - '0');
                else if (c >= 'a' && c <= 'f') value |= static_cast<uint32_t>(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') value |= static_cast<uint32_t>(c - 'A' + 10);
                else
                {
                    Error("invalid unicode escape");
                    return false;
                }
            }
            return true;
        }

        bool ParseString(std::string& out)
        {
            ++m_pos; // opening quote
            out.clear();
            while (m_pos < m_text.size())
            {
                char c = m_text[m_pos++];
                if (c == '"')
                    return true;
                if (c != '\\')
                {
                    out += c;
                    continue;
                }
                if (m_pos >= m_text.size())
                    break;
                char e = m_text[m_pos++];
                switch (e)
                {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                {
                    uint32_t cp = 0;
                    if (!ParseHex4(cp))
                        return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && m_pos + 6 <= m_text.size() && m_text[m_pos] == '\\' &&
                        m_text[m_pos + 1] == 'u')
                    {
                        m_pos += 2;
                        uint32_t low = 0;
                        if (!ParseHex4(low))
                            return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default:
                    Error("invalid escape sequence");
                    return false;
                }
            }
            Error("unterminated string");
            return false;
        }

        bool ParseArray(Json& out, int depth)
        {
            ++m_pos; // [
            out = Json::MakeArray();
            SkipWhitespace();
            if (m_pos < m_text.size() && m_text[m_pos] == ']')
            {
                ++m_pos;
                return true;
            }
            for (;;)
            {
                Json item;
                if (!ParseValue(item, depth + 1))
                    return false;
                out.m_items.push_back(std::move(item));
                SkipWhitespace();
                if (m_pos >= m_text.size())
                    break;
                char c = m_text[m_pos++];
                if (c == ']')
                    return true;
                if (c != ',')
                {
                    Error("expected ',' or ']'");
                    return false;
                }
                SkipWhitespace();
                if (m_pos < m_text.size() && m_text[m_pos] == ']') // trailing comma
                {
                    ++m_pos;
                    return true;
                }
            }
            Error("unterminated array");
            return false;
        }

        bool ParseObject(Json& out, int depth)
        {
            ++m_pos; // {
            out = Json::MakeObject();
            SkipWhitespace();
            if (m_pos < m_text.size() && m_text[m_pos] == '}')
            {
                ++m_pos;
                return true;
            }
            for (;;)
            {
                SkipWhitespace();
                if (m_pos >= m_text.size() || m_text[m_pos] != '"')
                {
                    Error("expected object key");
                    return false;
                }
                std::string key;
                if (!ParseString(key))
                    return false;
                SkipWhitespace();
                if (m_pos >= m_text.size() || m_text[m_pos] != ':')
                {
                    Error("expected ':'");
                    return false;
                }
                ++m_pos;
                Json value;
                if (!ParseValue(value, depth + 1))
                    return false;
                out.Set(key, std::move(value));
                SkipWhitespace();
                if (m_pos >= m_text.size())
                    break;
                char c = m_text[m_pos++];
                if (c == '}')
                    return true;
                if (c != ',')
                {
                    Error("expected ',' or '}'");
                    return false;
                }
                SkipWhitespace();
                if (m_pos < m_text.size() && m_text[m_pos] == '}') // trailing comma
                {
                    ++m_pos;
                    return true;
                }
            }
            Error("unterminated object");
            return false;
        }

        std::string_view m_text;
        size_t m_pos = 0;
        std::string m_error;
    };

    Json Json::MakeArray()
    {
        Json j;
        j.m_type = Type::Array;
        return j;
    }

    Json Json::MakeObject()
    {
        Json j;
        j.m_type = Type::Object;
        return j;
    }

    std::expected<Json, std::string> Json::Parse(std::string_view text)
    {
        return JsonParser(text).Run();
    }

    std::optional<Json> Json::Load(const fs::path& path)
    {
        auto text = ReadAllText(path);
        if (!text)
        {
            Log("[PlutoFall] Could not read JSON file {}", Narrow(path.wstring()));
            return std::nullopt;
        }
        auto parsed = Parse(*text);
        if (!parsed)
        {
            Log("[PlutoFall] {} in {}", parsed.error(), Narrow(path.wstring()));
            return std::nullopt;
        }
        return std::move(*parsed);
    }

    bool Json::AsBool(bool fallback) const noexcept
    {
        if (m_type == Type::Bool)
            return m_bool;
        if (m_type == Type::Number)
            return m_number != 0.0;
        if (m_type == Type::String)
            return m_string == "true" || m_string == "1" || m_string == "yes";
        return fallback;
    }

    double Json::AsDouble(double fallback) const noexcept
    {
        if (m_type == Type::Number)
            return m_number;
        if (m_type == Type::Bool)
            return m_bool ? 1.0 : 0.0;
        if (m_type == Type::String)
        {
            double value = 0.0;
            auto result = std::from_chars(m_string.data(), m_string.data() + m_string.size(), value);
            if (result.ec == std::errc())
                return value;
        }
        return fallback;
    }

    const std::string& Json::AsString() const noexcept
    {
        return m_type == Type::String ? m_string : EmptyString();
    }

    std::string Json::AsString(std::string_view fallback) const
    {
        if (m_type == Type::String)
            return m_string;
        if (m_type == Type::Number)
            return std::format("{}", m_number);
        if (m_type == Type::Bool)
            return m_bool ? "true" : "false";
        return std::string(fallback);
    }

    size_t Json::Size() const noexcept
    {
        return (m_type == Type::Array || m_type == Type::Object) ? m_items.size() : 0;
    }

    const Json& Json::operator[](size_t index) const noexcept
    {
        if ((m_type == Type::Array || m_type == Type::Object) && index < m_items.size())
            return m_items[index];
        return NullJson();
    }

    bool Json::Has(std::string_view key) const noexcept
    {
        if (m_type != Type::Object)
            return false;
        return std::find(m_keys.begin(), m_keys.end(), key) != m_keys.end();
    }

    const Json& Json::operator[](std::string_view key) const noexcept
    {
        if (m_type != Type::Object)
            return NullJson();
        for (size_t i = 0; i < m_keys.size(); ++i)
        {
            if (m_keys[i] == key)
                return m_items[i];
        }
        return NullJson();
    }

    float Json::GetFloat(std::string_view key, float fallback) const noexcept
    {
        return (*this)[key].AsFloat(fallback);
    }

    int Json::GetInt(std::string_view key, int fallback) const noexcept
    {
        return (*this)[key].AsInt(fallback);
    }

    bool Json::GetBool(std::string_view key, bool fallback) const noexcept
    {
        return (*this)[key].AsBool(fallback);
    }

    std::string Json::GetString(std::string_view key, std::string_view fallback) const
    {
        return (*this)[key].AsString(fallback);
    }

    void Json::Push(Json value)
    {
        if (m_type != Type::Array)
        {
            *this = MakeArray();
        }
        m_items.push_back(std::move(value));
    }

    void Json::Set(std::string_view key, Json value)
    {
        if (m_type != Type::Object)
        {
            *this = MakeObject();
        }
        for (size_t i = 0; i < m_keys.size(); ++i)
        {
            if (m_keys[i] == key)
            {
                m_items[i] = std::move(value);
                return;
            }
        }
        m_keys.emplace_back(key);
        m_items.push_back(std::move(value));
    }

    std::string Json::Serialize(bool pretty) const
    {
        std::string out;
        SerializeTo(out, pretty, 0);
        return out;
    }

    void Json::SerializeTo(std::string& out, bool pretty, int indent) const
    {
        auto newline = [&](int level)
        {
            if (!pretty)
                return;
            out += '\n';
            out.append(static_cast<size_t>(level) * 2, ' ');
        };
        auto writeString = [&](const std::string& s)
        {
            out += '"';
            for (char c : s)
            {
                switch (c)
                {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (static_cast<uint8_t>(c) < 0x20)
                        out += std::format("\\u{:04x}", static_cast<int>(c));
                    else
                        out += c;
                }
            }
            out += '"';
        };

        switch (m_type)
        {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += m_bool ? "true" : "false"; break;
        case Type::Number: out += std::format("{}", m_number); break;
        case Type::String: writeString(m_string); break;
        case Type::Array:
            out += '[';
            for (size_t i = 0; i < m_items.size(); ++i)
            {
                if (i > 0)
                    out += ',';
                newline(indent + 1);
                m_items[i].SerializeTo(out, pretty, indent + 1);
            }
            if (!m_items.empty())
                newline(indent);
            out += ']';
            break;
        case Type::Object:
            out += '{';
            for (size_t i = 0; i < m_items.size(); ++i)
            {
                if (i > 0)
                    out += ',';
                newline(indent + 1);
                writeString(m_keys[i]);
                out += pretty ? ": " : ":";
                m_items[i].SerializeTo(out, pretty, indent + 1);
            }
            if (!m_items.empty())
                newline(indent);
            out += '}';
            break;
        }
    }
}
