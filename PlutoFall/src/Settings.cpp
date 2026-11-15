#include "Settings.h"
#include "Json.h"
#include "Util.h"

namespace pf
{
    namespace
    {
        fs::path SettingsPath()
        {
            return Paths::UserData() / L"settings.json";
        }
    }

    void Settings::Load()
    {
        std::error_code ec;
        if (!fs::exists(SettingsPath(), ec))
            return;
        auto json = Json::Load(SettingsPath());
        if (!json)
            return;
        sfxVolume = Clamp01(json->GetFloat("sfxVolume", sfxVolume));
        musicVolume = Clamp01(json->GetFloat("musicVolume", musicVolume));
        language = json->GetString("language", language);
        fullscreen = json->GetBool("fullscreen", fullscreen);
    }

    void Settings::Save() const
    {
        Json json = Json::MakeObject();
        json.Set("sfxVolume", sfxVolume);
        json.Set("musicVolume", musicVolume);
        json.Set("language", language);
        json.Set("fullscreen", fullscreen);
        if (!WriteAllText(SettingsPath(), json.Serialize()))
            Log("[PlutoFall] Could not save settings to {}", Narrow(SettingsPath().wstring()));
    }
}
