// Util.h - string conversion, file IO, logging, random numbers and asset paths.
#pragma once
#include "Common.h"
#include <random>

namespace pf
{
    std::wstring Widen(std::string_view utf8);
    std::string Narrow(std::wstring_view wide);
    std::string ToLower(std::string_view text);

    std::optional<std::vector<uint8_t>> ReadAllBytes(const fs::path& path);
    std::optional<std::string> ReadAllText(const fs::path& path);
    bool WriteAllText(const fs::path& path, std::string_view text);

    void LogLine(std::string_view message);

    template <class... Args>
    void Log(std::format_string<Args...> fmt, Args&&... args)
    {
        LogLine(std::format(fmt, std::forward<Args>(args)...));
    }

    // Logs a failed HRESULT; returns true when the HRESULT indicates success.
    bool Check(HRESULT hr, std::string_view what);

    std::mt19937& Rng();
    float RandomFloat(float lo, float hi);
    int RandomInt(int lo, int hiInclusive);

    uint32_t HashString(std::string_view text);

    // Parses "#RGB", "#RRGGBB" or "#RRGGBBAA" (leading '#' optional).
    std::optional<Color> ParseColor(std::string_view text);
    Color ColorFromHash(uint32_t hash);
    Color RandomColor();

    // Resolves locations of game assets and per-user data.
    class Paths
    {
    public:
        // Searches for the "assets" folder next to the executable, in the
        // current directory and in parent folders of the executable.
        static void Initialize();
        static const fs::path& AssetRoot();
        // Converts a UTF-8 path relative to the asset root to an absolute path.
        static fs::path Asset(std::string_view relativeUtf8);
        // %LOCALAPPDATA%\PlutoFall
        static fs::path UserData();
    };
}
