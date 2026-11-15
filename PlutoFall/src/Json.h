// Json.h - a small self-contained JSON DOM and parser (UTF-8).
// Supports // and /* */ comments and trailing commas as conveniences for hand-written data files.
#pragma once
#include "Common.h"
#include <expected>

namespace pf
{
    class Json
    {
    public:
        enum class Type
        {
            Null,
            Bool,
            Number,
            String,
            Array,
            Object
        };

        Json() = default;
        Json(std::nullptr_t) {}
        Json(bool value) : m_type(Type::Bool), m_bool(value) {}
        Json(double value) : m_type(Type::Number), m_number(value) {}
        Json(float value) : m_type(Type::Number), m_number(value) {}
        Json(int value) : m_type(Type::Number), m_number(value) {}
        Json(const char* value) : m_type(Type::String), m_string(value) {}
        Json(std::string value) : m_type(Type::String), m_string(std::move(value)) {}

        static Json MakeArray();
        static Json MakeObject();

        static std::expected<Json, std::string> Parse(std::string_view text);
        // Loads and parses a file. Errors are logged; std::nullopt is returned on failure.
        static std::optional<Json> Load(const fs::path& path);

        Type GetType() const noexcept { return m_type; }
        bool IsNull() const noexcept { return m_type == Type::Null; }
        bool IsBool() const noexcept { return m_type == Type::Bool; }
        bool IsNumber() const noexcept { return m_type == Type::Number; }
        bool IsString() const noexcept { return m_type == Type::String; }
        bool IsArray() const noexcept { return m_type == Type::Array; }
        bool IsObject() const noexcept { return m_type == Type::Object; }

        bool AsBool(bool fallback = false) const noexcept;
        double AsDouble(double fallback = 0.0) const noexcept;
        float AsFloat(float fallback = 0.0f) const noexcept { return static_cast<float>(AsDouble(fallback)); }
        int AsInt(int fallback = 0) const noexcept { return static_cast<int>(AsDouble(fallback)); }
        const std::string& AsString() const noexcept;   // empty string when not a string
        std::string AsString(std::string_view fallback) const;

        // Array access. Out-of-range or non-array access yields a null value.
        size_t Size() const noexcept;
        const Json& operator[](size_t index) const noexcept;
        const Json& operator[](int index) const noexcept { return index < 0 ? (*this)[SIZE_MAX] : (*this)[static_cast<size_t>(index)]; }
        const std::vector<Json>& Items() const noexcept { return m_items; }

        // Object access. Missing keys or non-object access yields a null value.
        bool Has(std::string_view key) const noexcept;
        const Json& operator[](std::string_view key) const noexcept;
        const Json& operator[](const char* key) const noexcept { return (*this)[std::string_view(key)]; }
        const std::vector<std::string>& Keys() const noexcept { return m_keys; }

        // Convenience accessors on objects.
        float GetFloat(std::string_view key, float fallback) const noexcept;
        int GetInt(std::string_view key, int fallback) const noexcept;
        bool GetBool(std::string_view key, bool fallback) const noexcept;
        std::string GetString(std::string_view key, std::string_view fallback = {}) const;

        // Builders.
        void Push(Json value);
        void Set(std::string_view key, Json value);

        std::string Serialize(bool pretty = true) const;

    private:
        void SerializeTo(std::string& out, bool pretty, int indent) const;

        Type m_type = Type::Null;
        bool m_bool = false;
        double m_number = 0.0;
        std::string m_string;
        std::vector<Json> m_items;       // array items, or object values (parallel to m_keys)
        std::vector<std::string> m_keys; // object keys

        friend class JsonParser;
    };
}
