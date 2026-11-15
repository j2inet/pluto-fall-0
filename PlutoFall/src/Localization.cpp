#include "Localization.h"
#include "Json.h"
#include "Util.h"

namespace pf
{
    namespace
    {
        std::string g_current = "en";
        std::vector<LanguageInfo> g_languages;
        std::unordered_map<std::string, LocString> g_strings;

        std::string PrimarySubtag(std::string_view code)
        {
            auto dash = code.find_first_of("-_");
            return ToLower(code.substr(0, dash));
        }
    }

    LocString::LocString(std::string plain)
    {
        m_entries.emplace_back("", std::move(plain));
    }

    LocString LocString::FromJson(const Json& json)
    {
        LocString result;
        if (json.IsString())
        {
            result.m_entries.emplace_back("", json.AsString());
        }
        else if (json.IsObject())
        {
            for (size_t i = 0; i < json.Keys().size(); ++i)
                result.m_entries.emplace_back(ToLower(json.Keys()[i]), json[i].AsString(""));
        }
        else if (json.IsNumber() || json.IsBool())
        {
            result.m_entries.emplace_back("", json.AsString(""));
        }
        return result;
    }

    std::string LocString::Get() const
    {
        if (m_entries.empty())
            return {};
        std::vector<std::string> codes;
        codes.reserve(m_entries.size());
        for (const auto& entry : m_entries)
            codes.push_back(entry.first);
        int best = Localization::BestMatch(codes);
        return m_entries[static_cast<size_t>(best)].second;
    }

    std::wstring LocString::GetW() const
    {
        return Widen(Get());
    }

    void Localization::Load()
    {
        g_languages.clear();
        g_strings.clear();
        auto json = Json::Load(Paths::Asset("lang/strings.json"));
        if (json)
        {
            for (const auto& lang : (*json)["languages"].Items())
            {
                LanguageInfo info;
                info.code = ToLower(lang.GetString("code", "en"));
                info.name = lang.GetString("name", info.code);
                info.locale = Widen(lang.GetString("locale", info.code));
                g_languages.push_back(std::move(info));
            }
            const Json& strings = (*json)["strings"];
            for (size_t i = 0; i < strings.Keys().size(); ++i)
                g_strings[strings.Keys()[i]] = LocString::FromJson(strings[i]);
        }
        if (g_languages.empty())
            g_languages.push_back({ "en", "English", L"en-us" });
    }

    const std::string& Localization::Current()
    {
        return g_current;
    }

    void Localization::SetCurrent(std::string_view code)
    {
        const std::string lower = ToLower(code);
        for (const auto& lang : g_languages)
        {
            if (lang.code == lower)
            {
                g_current = lower;
                return;
            }
        }
        for (const auto& lang : g_languages)
        {
            if (lang.code == PrimarySubtag(lower))
            {
                g_current = lang.code;
                return;
            }
        }
        g_current = g_languages.empty() ? "en" : g_languages.front().code;
    }

    const std::vector<LanguageInfo>& Localization::Languages()
    {
        return g_languages;
    }

    std::wstring Localization::LocaleName()
    {
        for (const auto& lang : g_languages)
        {
            if (lang.code == g_current)
                return lang.locale;
        }
        return L"en-us";
    }

    std::string Localization::DetectSystemLanguage()
    {
        wchar_t buffer[LOCALE_NAME_MAX_LENGTH] = {};
        if (GetUserDefaultLocaleName(buffer, LOCALE_NAME_MAX_LENGTH) > 0)
        {
            const std::string code = ToLower(Narrow(buffer));
            for (const auto& lang : g_languages)
            {
                if (lang.code == code || lang.code == PrimarySubtag(code))
                    return lang.code;
            }
        }
        return "en";
    }

    std::string Localization::Get(std::string_view key)
    {
        auto it = g_strings.find(std::string(key));
        if (it == g_strings.end())
            return std::string(key);
        return it->second.Get();
    }

    std::wstring Localization::GetW(std::string_view key)
    {
        return Widen(Get(key));
    }

    int Localization::BestMatch(std::span<const std::string> codes)
    {
        if (codes.empty())
            return 0;
        const std::string primary = PrimarySubtag(g_current);
        int exact = -1, partial = -1, english = -1, neutral = -1;
        for (size_t i = 0; i < codes.size(); ++i)
        {
            const std::string& code = codes[i];
            const int index = static_cast<int>(i);
            if (code == g_current && exact < 0) exact = index;
            else if (PrimarySubtag(code) == primary && partial < 0) partial = index;
            if (code.empty() && neutral < 0) neutral = index;
            if (PrimarySubtag(code) == "en" && english < 0) english = index;
        }
        if (exact >= 0) return exact;
        if (partial >= 0) return partial;
        if (neutral >= 0) return neutral;
        if (english >= 0) return english;
        return 0;
    }
}
