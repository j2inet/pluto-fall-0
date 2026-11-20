#include "LosslessJson.h"
#include <algorithm>
#include <charconv>
#include <format>

namespace de::json
{
    namespace
    {
        bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

        // Splits trailing whitespace off a string; returns it.
        std::string TakeTrailingSpace(std::string& s)
        {
            size_t end = s.size();
            while (end > 0 && IsSpace(s[end - 1]))
                --end;
            std::string tail = s.substr(end);
            s.erase(end);
            return tail;
        }

        bool IsWhitespaceOnly(std::string_view s)
        {
            return std::ranges::all_of(s, IsSpace);
        }

        // Indentation of the last line in a run of trivia, and whether it contains a newline.
        struct LineInfo
        {
            bool multiline = false;
            std::string indent;
        };

        LineInfo LastLine(std::string_view trivia)
        {
            LineInfo info;
            const size_t nl = trivia.rfind('\n');
            if (nl == std::string_view::npos)
                return info;
            info.multiline = true;
            for (size_t i = nl + 1; i < trivia.size() && (trivia[i] == ' ' || trivia[i] == '\t'); ++i)
                info.indent += trivia[i];
            return info;
        }

        void AppendUtf8(std::string& out, uint32_t cp)
        {
            if (cp < 0x80)
                out += static_cast<char>(cp);
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

        void Write(const Node& node, std::string& out)
        {
            switch (node.kind)
            {
            case Kind::Null: out += node.raw.empty() ? "null" : node.raw; break;
            case Kind::Bool: out += node.raw.empty() ? (node.boolean ? "true" : "false") : node.raw; break;
            case Kind::Number: out += node.raw.empty() ? node.text : node.raw; break;
            case Kind::String: out += node.raw.empty() ? Quote(node.text) : node.raw; break;
            case Kind::Array:
            case Kind::Object:
            {
                const bool object = node.kind == Kind::Object;
                out += object ? '{' : '[';
                for (size_t i = 0; i < node.elements.size(); ++i)
                {
                    const Element& e = node.elements[i];
                    out += e.prefix;
                    if (object)
                    {
                        out += e.keyRaw.empty() ? Quote(e.key) : e.keyRaw;
                        out += e.beforeColon;
                        out += ':';
                        out += e.afterColon;
                    }
                    if (e.value)
                        Write(*e.value, out);
                    else
                        out += "null";
                    out += e.suffix;
                    if (i + 1 < node.elements.size() || node.trailingComma)
                        out += ',';
                }
                out += node.closing;
                out += object ? '}' : ']';
                break;
            }
            }
        }
    }

    // ------------------------------------------------------------------ Node

    int Node::IndexOf(std::string_view key) const noexcept
    {
        if (kind != Kind::Object)
            return -1;
        for (size_t i = 0; i < elements.size(); ++i)
        {
            if (elements[i].key == key)
                return static_cast<int>(i);
        }
        return -1;
    }

    Node* Node::Find(std::string_view key) noexcept
    {
        const int index = IndexOf(key);
        return index < 0 ? nullptr : elements[static_cast<size_t>(index)].value.get();
    }

    const Node* Node::Find(std::string_view key) const noexcept
    {
        const int index = IndexOf(key);
        return index < 0 ? nullptr : elements[static_cast<size_t>(index)].value.get();
    }

    const std::string& Node::Str() const noexcept
    {
        static const std::string empty;
        return kind == Kind::String ? text : empty;
    }

    NodePtr MakeNull() { return std::make_unique<Node>(); }

    NodePtr MakeBool(bool value)
    {
        auto n = std::make_unique<Node>();
        n->kind = Kind::Bool;
        n->boolean = value;
        return n;
    }

    NodePtr MakeString(std::string_view value)
    {
        auto n = std::make_unique<Node>();
        n->kind = Kind::String;
        n->text = value;
        return n;
    }

    NodePtr MakeObject()
    {
        auto n = std::make_unique<Node>();
        n->kind = Kind::Object;
        return n;
    }

    NodePtr MakeArray()
    {
        auto n = std::make_unique<Node>();
        n->kind = Kind::Array;
        return n;
    }

    NodePtr Clone(const Node& node)
    {
        auto copy = std::make_unique<Node>();
        copy->kind = node.kind;
        copy->boolean = node.boolean;
        copy->text = node.text;
        copy->raw = node.raw;
        copy->closing = node.closing;
        copy->trailingComma = node.trailingComma;
        copy->indent = node.indent;
        for (const Element& e : node.elements)
        {
            Element c;
            c.prefix = e.prefix;
            c.key = e.key;
            c.keyRaw = e.keyRaw;
            c.beforeColon = e.beforeColon;
            c.afterColon = e.afterColon;
            c.suffix = e.suffix;
            if (e.value)
                c.value = Clone(*e.value);
            copy->elements.push_back(std::move(c));
        }
        return copy;
    }

    std::string Quote(std::string_view value)
    {
        std::string out = "\"";
        for (char c : value)
        {
            switch (c)
            {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                    out += std::format("\\u{:04x}", static_cast<unsigned>(static_cast<unsigned char>(c)));
                else
                    out += c;
            }
        }
        out += '"';
        return out;
    }

    // ------------------------------------------------------------------ Parser

    class Parser
    {
    public:
        Parser(std::string_view text, Document& doc) : m_text(text), m_doc(doc) {}

        bool Run(std::string& error)
        {
            if (m_text.starts_with("\xEF\xBB\xBF"))
            {
                m_doc.m_bom = true;
                m_pos = 3;
            }
            if (m_text.find("\r\n") != std::string_view::npos)
                m_doc.m_newline = "\r\n";

            m_doc.m_leading = Trivia();
            if (!m_ok)
                return Fail(error);
            m_doc.m_root = Value(0);
            if (!m_ok)
                return Fail(error);
            m_doc.m_trailing = Trivia();
            if (!m_ok)
                return Fail(error);
            if (m_pos != m_text.size())
            {
                SetError("unexpected text after the end of the document");
                return Fail(error);
            }
            if (!m_unit.empty())
                m_doc.m_unit = m_unit;
            return true;
        }

    private:
        bool Fail(std::string& error)
        {
            size_t line = 1, column = 1;
            for (size_t i = 0; i < m_errorPos && i < m_text.size(); ++i)
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
            error = std::format("line {}, column {}: {}", line, column, m_error);
            return false;
        }

        void SetError(std::string_view message)
        {
            if (!m_ok)
                return;
            m_ok = false;
            m_error = message;
            m_errorPos = m_pos;
        }

        bool AtEnd() const { return m_pos >= m_text.size(); }
        char Peek() const { return AtEnd() ? '\0' : m_text[m_pos]; }

        // Whitespace and comments.
        std::string Trivia()
        {
            const size_t start = m_pos;
            while (!AtEnd())
            {
                const char c = m_text[m_pos];
                if (IsSpace(c))
                {
                    ++m_pos;
                }
                else if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '/')
                {
                    while (!AtEnd() && m_text[m_pos] != '\n')
                        ++m_pos;
                }
                else if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '*')
                {
                    const size_t end = m_text.find("*/", m_pos + 2);
                    if (end == std::string_view::npos)
                    {
                        SetError("unterminated /* comment");
                        m_pos = m_text.size();
                        break;
                    }
                    m_pos = end + 2;
                }
                else
                {
                    break;
                }
            }
            return std::string(m_text.substr(start, m_pos - start));
        }

        std::string LineIndentAt(size_t pos) const
        {
            size_t lineStart = m_text.rfind('\n', pos == 0 ? 0 : pos - 1);
            lineStart = (lineStart == std::string_view::npos || pos == 0) ? 0 : lineStart + 1;
            if (m_doc.m_bom && lineStart == 0)
                lineStart = 3;
            std::string indent;
            for (size_t i = lineStart; i < pos && (m_text[i] == ' ' || m_text[i] == '\t'); ++i)
                indent += m_text[i];
            return indent;
        }

        NodePtr Value(int depth)
        {
            if (depth > 512)
            {
                SetError("nesting too deep");
                return MakeNull();
            }
            if (AtEnd())
            {
                SetError("unexpected end of file");
                return MakeNull();
            }
            const char c = Peek();
            if (c == '{' || c == '[')
                return Container(depth);
            if (c == '"')
            {
                auto n = std::make_unique<Node>();
                n->kind = Kind::String;
                const size_t start = m_pos;
                n->text = String();
                n->raw = std::string(m_text.substr(start, m_pos - start));
                return n;
            }
            if (c == '-' || (c >= '0' && c <= '9'))
            {
                const size_t start = m_pos;
                ++m_pos;
                while (!AtEnd())
                {
                    const char d = Peek();
                    if ((d >= '0' && d <= '9') || d == '.' || d == 'e' || d == 'E' || d == '+' || d == '-')
                        ++m_pos;
                    else
                        break;
                }
                auto n = std::make_unique<Node>();
                n->kind = Kind::Number;
                n->raw = std::string(m_text.substr(start, m_pos - start));
                n->text = n->raw;
                double check = 0;
                auto result = std::from_chars(n->raw.data(), n->raw.data() + n->raw.size(), check);
                if (result.ec != std::errc() || result.ptr != n->raw.data() + n->raw.size())
                {
                    m_pos = start;
                    SetError("invalid number");
                }
                return n;
            }
            for (std::string_view word : { std::string_view("true"), std::string_view("false"), std::string_view("null") })
            {
                if (m_text.substr(m_pos, word.size()) == word)
                {
                    auto n = std::make_unique<Node>();
                    n->kind = word == "null" ? Kind::Null : Kind::Bool;
                    n->boolean = word == "true";
                    n->raw = std::string(word);
                    m_pos += word.size();
                    return n;
                }
            }
            SetError(std::format("unexpected character '{}'", c));
            return MakeNull();
        }

        uint32_t Hex4()
        {
            if (m_pos + 4 > m_text.size())
            {
                SetError("truncated \\u escape");
                return 0;
            }
            uint32_t value = 0;
            for (int i = 0; i < 4; ++i)
            {
                const char c = m_text[m_pos++];
                value <<= 4;
                if (c >= '0' && c <= '9') value |= static_cast<uint32_t>(c - '0');
                else if (c >= 'a' && c <= 'f') value |= static_cast<uint32_t>(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') value |= static_cast<uint32_t>(c - 'A' + 10);
                else
                {
                    SetError("invalid \\u escape");
                    return 0;
                }
            }
            return value;
        }

        std::string String()
        {
            std::string out;
            ++m_pos; // opening quote
            while (!AtEnd())
            {
                const char c = m_text[m_pos++];
                if (c == '"')
                    return out;
                if (c != '\\')
                {
                    out += c;
                    continue;
                }
                if (AtEnd())
                    break;
                const char e = m_text[m_pos++];
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
                    uint32_t cp = Hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF && m_text.substr(m_pos, 2) == "\\u")
                    {
                        m_pos += 2;
                        const uint32_t low = Hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default:
                    --m_pos;
                    SetError("invalid escape sequence");
                    return out;
                }
            }
            SetError("unterminated string");
            return out;
        }

        NodePtr Container(int depth)
        {
            auto node = std::make_unique<Node>();
            const bool object = Peek() == '{';
            const char close = object ? '}' : ']';
            node->kind = object ? Kind::Object : Kind::Array;
            node->indent = LineIndentAt(m_pos);
            ++m_pos;

            std::string trivia = Trivia();
            for (;;)
            {
                if (!m_ok)
                    return node;
                if (AtEnd())
                {
                    SetError(object ? "unterminated object" : "unterminated array");
                    return node;
                }
                if (Peek() == close)
                {
                    // Empty container, or the end after a trailing comma.
                    node->closing = std::move(trivia);
                    ++m_pos;
                    break;
                }

                Element e;
                e.prefix = std::move(trivia);
                if (object)
                {
                    if (Peek() != '"')
                    {
                        SetError("expected a quoted key");
                        return node;
                    }
                    const size_t start = m_pos;
                    e.key = String();
                    e.keyRaw = std::string(m_text.substr(start, m_pos - start));
                    e.beforeColon = Trivia();
                    if (Peek() != ':')
                    {
                        SetError("expected ':'");
                        return node;
                    }
                    ++m_pos;
                    e.afterColon = Trivia();
                }
                e.value = Value(depth + 1);
                if (!m_ok)
                    return node;
                e.suffix = Trivia();

                // Learn the indentation unit from the first multi-line container.
                if (m_unit.empty())
                {
                    const LineInfo line = LastLine(e.prefix);
                    if (line.multiline && line.indent.size() > node->indent.size() && line.indent.starts_with(node->indent))
                        m_unit = line.indent.substr(node->indent.size());
                }
                node->elements.push_back(std::move(e));

                if (Peek() == ',')
                {
                    ++m_pos;
                    trivia = Trivia();
                    if (Peek() == close)
                        node->trailingComma = true;
                    continue;
                }
                if (Peek() == close)
                {
                    ++m_pos;
                    break;
                }
                SetError(object ? "expected ',' or '}'" : "expected ',' or ']'");
                return node;
            }
            return node;
        }

        std::string_view m_text;
        Document& m_doc;
        size_t m_pos = 0;
        bool m_ok = true;
        std::string m_error;
        size_t m_errorPos = 0;
        std::string m_unit;
    };

    // ------------------------------------------------------------------ Document

    bool Document::Parse(std::string_view text, std::string& error)
    {
        *this = Document{};
        Parser parser(text, *this);
        if (!parser.Run(error))
        {
            m_root.reset();
            return false;
        }
        return true;
    }

    std::string Document::Serialize() const
    {
        std::string out;
        if (m_bom)
            out += "\xEF\xBB\xBF";
        out += m_leading;
        if (m_root)
            Write(*m_root, out);
        out += m_trailing;
        return out;
    }

    std::string Document::DetachTail(Node& container)
    {
        if (container.elements.empty())
            return {};
        return TakeTrailingSpace(container.elements.back().suffix);
    }

    void Document::AttachTail(Node& container, std::string tail)
    {
        if (container.elements.empty())
        {
            // Collapse to "[]" / "{}" unless the closing trivia holds a comment.
            container.trailingComma = false;
            if (IsWhitespaceOnly(container.closing))
                container.closing.clear();
            else
                container.closing += tail;
            return;
        }
        container.elements.back().suffix += tail;
    }

    void Document::InsertElement(Node& container, size_t index, Element element)
    {
        index = std::min(index, container.elements.size());
        std::string elementIndent;
        std::string tail;

        if (container.elements.empty())
        {
            // First element: put it on its own line, one level deeper than the container.
            std::string before = container.closing;
            TakeTrailingSpace(before); // keep any comment that was inside the empty container
            elementIndent = container.indent + m_unit;
            element.prefix = before + m_newline + elementIndent;
            tail = m_newline + container.indent;
            container.closing.clear();
            container.trailingComma = false;
        }
        else
        {
            tail = DetachTail(container);
            const Element& reference = container.elements[index > 0 ? index - 1 : 0];
            const LineInfo line = LastLine(reference.prefix);
            if (line.multiline)
            {
                elementIndent = line.indent;
                element.prefix = m_newline + elementIndent;
            }
            else
            {
                elementIndent = container.indent;
                element.prefix = index == 0 ? "" : " ";
                if (index == 0)
                    container.elements[0].prefix = " " + container.elements[0].prefix;
            }
        }

        if (element.value && element.value->IsContainer() && element.value->indent.empty())
            element.value->indent = elementIndent;
        container.elements.insert(container.elements.begin() + static_cast<std::ptrdiff_t>(index), std::move(element));
        AttachTail(container, std::move(tail));
    }

    Node* Document::Set(Node& object, std::string_view key, NodePtr value)
    {
        if (!object.IsObject() || !value)
            return nullptr;
        const int index = object.IndexOf(key);
        if (index >= 0)
        {
            Element& e = object.elements[static_cast<size_t>(index)];
            if (value->IsContainer() && value->indent.empty())
            {
                const LineInfo line = LastLine(e.prefix);
                value->indent = line.multiline ? line.indent : object.indent + m_unit;
            }
            e.value = std::move(value);
            return e.value.get();
        }
        Element e;
        e.key = key;
        e.keyRaw = Quote(key);
        e.afterColon = " ";
        e.value = std::move(value);
        Node* stored = e.value.get();
        InsertElement(object, object.elements.size(), std::move(e));
        return stored;
    }

    Node* Document::SetString(Node& object, std::string_view key, std::string_view value)
    {
        if (Node* existing = object.Find(key); existing && existing->IsString())
        {
            if (existing->text != value)
            {
                existing->text = value;
                existing->raw.clear();
            }
            return existing;
        }
        return Set(object, key, MakeString(value));
    }

    bool Document::Remove(Node& object, std::string_view key)
    {
        const int index = object.IndexOf(key);
        if (index < 0)
            return false;
        RemoveAt(object, static_cast<size_t>(index));
        return true;
    }

    Node* Document::Insert(Node& array, size_t index, NodePtr value)
    {
        if (!array.IsArray() || !value)
            return nullptr;
        Element e;
        e.value = std::move(value);
        Node* stored = e.value.get();
        InsertElement(array, index, std::move(e));
        return stored;
    }

    void Document::RemoveAt(Node& container, size_t index)
    {
        if (index >= container.elements.size())
            return;
        std::string tail = DetachTail(container);
        container.elements.erase(container.elements.begin() + static_cast<std::ptrdiff_t>(index));
        AttachTail(container, std::move(tail));
    }

    void Document::Move(Node& container, size_t from, size_t to)
    {
        auto& v = container.elements;
        if (from >= v.size() || to >= v.size() || from == to)
            return;
        std::string tail = DetachTail(container);
        // Keep the prefixes in place (the first element often differs from the rest),
        // so formatting stays tidy; only the values, keys and suffixes move.
        std::vector<std::string> prefixes;
        for (const Element& e : v)
            prefixes.push_back(e.prefix);
        Element moving = std::move(v[from]);
        v.erase(v.begin() + static_cast<std::ptrdiff_t>(from));
        v.insert(v.begin() + static_cast<std::ptrdiff_t>(to), std::move(moving));
        for (size_t i = 0; i < v.size(); ++i)
        {
            // Comments written above an element travel with it; plain whitespace stays put.
            if (IsWhitespaceOnly(v[i].prefix) && IsWhitespaceOnly(prefixes[i]))
                v[i].prefix = prefixes[i];
        }
        AttachTail(container, std::move(tail));
    }
}
