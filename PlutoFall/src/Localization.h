// Localization.h - multi-language strings.
//
// A "multi-language string" in any JSON file may be either a plain string (used for
// every language) or an object mapping language codes to text, e.g.
//     { "en": "Start Game", "es": "Comenzar", "ja": "ゲームスタート" }
#pragma once
#include "Common.h"

namespace pf
{
    class Json;

    struct LanguageInfo
    {
        std::string code;      // e.g. "en"
        std::string name;      // native display name, e.g. "Español"
        std::wstring locale;   // DirectWrite locale, e.g. L"es-es"
    };

    class LocString
    {
    public:
        LocString() = default;
        explicit LocString(std::string plain);

        static LocString FromJson(const Json& json);

        // Text for the current language (falls back to English, then any language).
        std::string Get() const;
        std::wstring GetW() const;
        bool Empty() const noexcept { return m_entries.empty(); }

    private:
        std::vector<std::pair<std::string, std::string>> m_entries;
    };

    class Localization
    {
    public:
        // Loads assets/lang/strings.json.
        static void Load();

        static const std::string& Current();
        static void SetCurrent(std::string_view code);
        static const std::vector<LanguageInfo>& Languages();
        static std::wstring LocaleName();

        // Picks the best supported language for the user's Windows UI language.
        static std::string DetectSystemLanguage();

        // Looks up a UI string by key (e.g. "menu.start"). Returns the key if missing.
        static std::string Get(std::string_view key);
        static std::wstring GetW(std::string_view key);

        // Chooses the best match for the current language from a list of language codes.
        static int BestMatch(std::span<const std::string> codes);
    };
}
