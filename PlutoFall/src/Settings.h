// Settings.h - user options persisted to %LOCALAPPDATA%\PlutoFall\settings.json
#pragma once
#include "Common.h"

namespace pf
{
    struct Settings
    {
        float sfxVolume = 0.8f;
        float musicVolume = 0.6f;
        std::string language;   // empty = detect from Windows
        bool fullscreen = false;

        void Load();
        void Save() const;
    };
}
