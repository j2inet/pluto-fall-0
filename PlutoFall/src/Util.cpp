#include "Util.h"
#include <fstream>
#include <shlobj.h>

namespace pf
{
    std::wstring Widen(std::string_view utf8)
    {
        if (utf8.empty())
            return {};
        int count = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
        std::wstring result(static_cast<size_t>(count), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), result.data(), count);
        return result;
    }

    std::string Narrow(std::wstring_view wide)
    {
        if (wide.empty())
            return {};
        int count = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<size_t>(count), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), count, nullptr, nullptr);
        return result;
    }

    std::string ToLower(std::string_view text)
    {
        std::string result(text);
        for (char& c : result)
        {
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        }
        return result;
    }

    std::optional<std::vector<uint8_t>> ReadAllBytes(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            return std::nullopt;
        const auto size = static_cast<size_t>(file.tellg());
        std::vector<uint8_t> data(size);
        file.seekg(0);
        if (size > 0 && !file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size)))
            return std::nullopt;
        return data;
    }

    std::optional<std::string> ReadAllText(const fs::path& path)
    {
        auto bytes = ReadAllBytes(path);
        if (!bytes)
            return std::nullopt;
        std::string text(bytes->begin(), bytes->end());
        // Strip a UTF-8 byte order mark if present.
        if (text.size() >= 3 && static_cast<uint8_t>(text[0]) == 0xEF && static_cast<uint8_t>(text[1]) == 0xBB &&
            static_cast<uint8_t>(text[2]) == 0xBF)
        {
            text.erase(0, 3);
        }
        return text;
    }

    bool WriteAllText(const fs::path& path, std::string_view text)
    {
        std::error_code ec;
        if (path.has_parent_path())
            fs::create_directories(path.parent_path(), ec);
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file)
            return false;
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        return static_cast<bool>(file);
    }

    void LogLine(std::string_view message)
    {
        std::wstring wide = Widen(message);
        wide += L"\n";
        OutputDebugStringW(wide.c_str());
    }

    bool Check(HRESULT hr, std::string_view what)
    {
        if (SUCCEEDED(hr))
            return true;
        Log("[PlutoFall] {} failed (HRESULT 0x{:08X})", what, static_cast<uint32_t>(hr));
        return false;
    }

    std::mt19937& Rng()
    {
        static std::mt19937 engine{ std::random_device{}() };
        return engine;
    }

    float RandomFloat(float lo, float hi)
    {
        std::uniform_real_distribution<float> dist(lo, hi);
        return dist(Rng());
    }

    int RandomInt(int lo, int hiInclusive)
    {
        std::uniform_int_distribution<int> dist(lo, hiInclusive);
        return dist(Rng());
    }

    uint32_t HashString(std::string_view text)
    {
        // FNV-1a
        uint32_t hash = 2166136261u;
        for (char c : text)
        {
            hash ^= static_cast<uint8_t>(c);
            hash *= 16777619u;
        }
        return hash;
    }

    std::optional<Color> ParseColor(std::string_view text)
    {
        if (!text.empty() && text.front() == '#')
            text.remove_prefix(1);
        auto hex = [](char c) -> int
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        for (char c : text)
        {
            if (hex(c) < 0)
                return std::nullopt;
        }
        auto byteAt = [&](size_t i) { return static_cast<float>(hex(text[i]) * 16 + hex(text[i + 1])) / 255.0f; };
        switch (text.size())
        {
        case 3:
            return Color{ hex(text[0]) / 15.0f, hex(text[1]) / 15.0f, hex(text[2]) / 15.0f, 1.0f };
        case 6:
            return Color{ byteAt(0), byteAt(2), byteAt(4), 1.0f };
        case 8:
            return Color{ byteAt(0), byteAt(2), byteAt(4), byteAt(6) };
        default:
            return std::nullopt;
        }
    }

    Color ColorFromHash(uint32_t hash)
    {
        // Use the hash as a hue so colours are vivid rather than muddy.
        const float hue = static_cast<float>(hash % 360u) / 60.0f;
        const float x = 1.0f - std::fabs(std::fmod(hue, 2.0f) - 1.0f);
        Color c{};
        switch (static_cast<int>(hue))
        {
        case 0: c = { 1, x, 0, 1 }; break;
        case 1: c = { x, 1, 0, 1 }; break;
        case 2: c = { 0, 1, x, 1 }; break;
        case 3: c = { 0, x, 1, 1 }; break;
        case 4: c = { x, 0, 1, 1 }; break;
        default: c = { 1, 0, x, 1 }; break;
        }
        // Soften a little.
        c.r = 0.25f + c.r * 0.7f;
        c.g = 0.25f + c.g * 0.7f;
        c.b = 0.25f + c.b * 0.7f;
        return c;
    }

    Color RandomColor()
    {
        return ColorFromHash(static_cast<uint32_t>(Rng()()));
    }

    namespace
    {
        fs::path g_assetRoot;

        fs::path ExecutableDirectory()
        {
            std::wstring buffer(MAX_PATH, L'\0');
            for (;;)
            {
                DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
                if (length < buffer.size())
                {
                    buffer.resize(length);
                    break;
                }
                buffer.resize(buffer.size() * 2);
            }
            return fs::path(buffer).parent_path();
        }
    }

    void Paths::Initialize()
    {
        std::error_code ec;
        std::vector<fs::path> candidates;
        const fs::path exeDir = ExecutableDirectory();
        candidates.push_back(exeDir / L"assets");
        candidates.push_back(fs::current_path(ec) / L"assets");
        fs::path probe = exeDir;
        for (int i = 0; i < 5 && probe.has_parent_path() && probe != probe.parent_path(); ++i)
        {
            probe = probe.parent_path();
            candidates.push_back(probe / L"assets");
            candidates.push_back(probe / L"PlutoFall" / L"assets");
        }
        for (const auto& candidate : candidates)
        {
            if (fs::exists(candidate / L"data" / L"game.json", ec))
            {
                g_assetRoot = fs::weakly_canonical(candidate, ec);
                Log("[PlutoFall] Asset root: {}", Narrow(g_assetRoot.wstring()));
                return;
            }
        }
        g_assetRoot = exeDir / L"assets";
        Log("[PlutoFall] Asset folder not found; defaulting to {}", Narrow(g_assetRoot.wstring()));
    }

    const fs::path& Paths::AssetRoot()
    {
        return g_assetRoot;
    }

    fs::path Paths::Asset(std::string_view relativeUtf8)
    {
        if (relativeUtf8.empty())
            return {};
        fs::path relative(Widen(relativeUtf8));
        if (relative.is_absolute())
            return relative;
        return (g_assetRoot / relative).lexically_normal();
    }

    fs::path Paths::UserData()
    {
        PWSTR raw = nullptr;
        fs::path result;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)))
        {
            result = fs::path(raw) / L"PlutoFall";
        }
        else
        {
            result = ExecutableDirectory();
        }
        CoTaskMemFree(raw);
        return result;
    }
}
