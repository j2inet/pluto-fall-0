// LosslessJson.h - a JSON document model that round-trips its source text.
//
// Everything that is not a value -- whitespace, line endings, // and /* */ comments,
// trailing commas -- is kept as "trivia" attached to the surrounding elements, and every
// scalar keeps its original token text. Serialising an unmodified document reproduces the
// input byte for byte. Edits only re-generate the text of the values that changed, and new
// elements copy the indentation of their siblings, so hand-written formatting, comments and
// attributes the editor doesn't know about survive a load/save cycle.
//
// This file has no Windows dependencies.
#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace de::json
{
    enum class Kind
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    struct Node;
    using NodePtr = std::unique_ptr<Node>;

    // One array item or object member, with the trivia around it.
    struct Element
    {
        std::string prefix;      // trivia before the key (objects) or value (arrays)
        std::string key;         // decoded key (objects only)
        std::string keyRaw;      // key token as written, including quotes
        std::string beforeColon; // trivia between key and ':'
        std::string afterColon;  // trivia between ':' and value
        NodePtr value;
        std::string suffix;      // trivia after the value, before ',' or the closing bracket
    };

    struct Node
    {
        Kind kind = Kind::Null;
        bool boolean = false;
        std::string text;   // decoded string value; number text for numbers
        std::string raw;    // original token for scalars; empty means "regenerate on save"

        // Containers.
        std::vector<Element> elements;
        std::string closing;        // trivia before the closing bracket when it is not in the last suffix
        bool trailingComma = false; // the source had a comma after the last element
        std::string indent;         // leading whitespace of the line the container starts on

        bool IsObject() const noexcept { return kind == Kind::Object; }
        bool IsArray() const noexcept { return kind == Kind::Array; }
        bool IsString() const noexcept { return kind == Kind::String; }
        bool IsContainer() const noexcept { return kind == Kind::Object || kind == Kind::Array; }

        size_t Size() const noexcept { return IsContainer() ? elements.size() : 0; }
        Node* At(size_t index) noexcept { return index < Size() ? elements[index].value.get() : nullptr; }
        const Node* At(size_t index) const noexcept { return index < Size() ? elements[index].value.get() : nullptr; }

        // Object members. Find returns nullptr when absent (or when this is not an object).
        int IndexOf(std::string_view key) const noexcept;
        Node* Find(std::string_view key) noexcept;
        const Node* Find(std::string_view key) const noexcept;

        // The string value, or an empty string for any other kind.
        const std::string& Str() const noexcept;
    };

    NodePtr MakeNull();
    NodePtr MakeBool(bool value);
    NodePtr MakeString(std::string_view value);
    NodePtr MakeObject();
    NodePtr MakeArray();
    NodePtr Clone(const Node& node);

    // Quotes and escapes a string for JSON output (UTF-8 is written as-is).
    std::string Quote(std::string_view value);

    class Document
    {
    public:
        // Parses text. On failure returns false and fills `error` (with line and column).
        bool Parse(std::string_view text, std::string& error);
        std::string Serialize() const;

        Node* Root() noexcept { return m_root.get(); }
        const Node* Root() const noexcept { return m_root.get(); }

        // Sets (replaces or appends) an object member. Returns the stored value.
        Node* Set(Node& object, std::string_view key, NodePtr value);
        // Replaces or creates a string member, keeping the member's surrounding trivia.
        Node* SetString(Node& object, std::string_view key, std::string_view value);
        bool Remove(Node& object, std::string_view key);

        // Array/object element operations.
        Node* Insert(Node& array, size_t index, NodePtr value);
        void RemoveAt(Node& container, size_t index);
        void Move(Node& container, size_t from, size_t to);

        const std::string& Newline() const noexcept { return m_newline; }
        const std::string& IndentUnit() const noexcept { return m_unit; }

    private:
        friend class Parser;

        void InsertElement(Node& container, size_t index, Element element);
        std::string DetachTail(Node& container);
        void AttachTail(Node& container, std::string tail);

        NodePtr m_root;
        std::string m_leading;  // trivia before the root value
        std::string m_trailing; // trivia after the root value
        bool m_bom = false;
        std::string m_newline = "\n";
        std::string m_unit = "  ";
    };
}
