// Audio.h - XAudio2 sound effects and music. Files are decoded with Media Foundation,
// so WAV, MP3 and MP4/M4A (AAC) are all supported.
#pragma once
#include "Common.h"
#include <xaudio2.h>

namespace pf
{
    struct SoundData
    {
        std::vector<uint8_t> format; // WAVEFORMATEX (+ extra bytes)
        std::vector<uint8_t> samples;

        const WAVEFORMATEX* Format() const { return reinterpret_cast<const WAVEFORMATEX*>(format.data()); }
    };

    class Audio
    {
    public:
        enum class Bus
        {
            Effects,
            Music
        };

        Audio() = default;
        ~Audio();
        Audio(const Audio&) = delete;
        Audio& operator=(const Audio&) = delete;

        bool Initialize();
        void Shutdown();
        void Update();

        // Paths are relative to the asset folder. Unknown/missing files are silently ignored.
        std::shared_ptr<SoundData> Load(std::string_view relativePath);

        void PlaySfx(std::string_view relativePath, float volume = 1.0f, float pitch = 1.0f);

        // Starts a looping sound and returns a handle (0 = failed).
        int StartLoop(std::string_view relativePath, Bus bus = Bus::Effects, float volume = 1.0f);
        void StopLoop(int handle);

        // Plays looping background music. Calling again with the same file keeps it playing.
        void PlayMusic(std::string_view relativePath);
        void StopMusic();

        void SetEffectsVolume(float volume);
        void SetMusicVolume(float volume);

    private:
        IXAudio2SourceVoice* CreateVoice(const SoundData& sound, Bus bus);

        struct ActiveVoice
        {
            IXAudio2SourceVoice* voice = nullptr;
            std::shared_ptr<SoundData> sound; // keeps sample memory alive while playing
        };

        ComPtr<IXAudio2> m_xaudio;
        IXAudio2MasteringVoice* m_master = nullptr;
        IXAudio2SubmixVoice* m_effectsBus = nullptr;
        IXAudio2SubmixVoice* m_musicBus = nullptr;
        bool m_mfStarted = false;

        std::unordered_map<std::string, std::shared_ptr<SoundData>> m_cache;
        std::vector<ActiveVoice> m_oneShots;
        std::unordered_map<int, ActiveVoice> m_loops;
        std::unordered_map<std::string, ULONGLONG> m_lastPlayed;
        int m_nextLoopHandle = 1;

        ActiveVoice m_music;
        std::string m_musicName;
    };
}
