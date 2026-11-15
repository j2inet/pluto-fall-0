#include "Audio.h"
#include "Util.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

namespace pf
{
    namespace
    {
        constexpr size_t MaxOneShotVoices = 48;

        std::shared_ptr<SoundData> DecodeWithMediaFoundation(const fs::path& path)
        {
            ComPtr<IMFSourceReader> reader;
            if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader)))
                return nullptr;

            const DWORD audioStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
            reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
            if (FAILED(reader->SetStreamSelection(audioStream, TRUE)))
                return nullptr;

            // Ask the reader to decode to PCM.
            ComPtr<IMFMediaType> requested;
            if (FAILED(MFCreateMediaType(&requested)))
                return nullptr;
            requested->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            requested->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
            if (!Check(reader->SetCurrentMediaType(audioStream, nullptr, requested.Get()), "SetCurrentMediaType(PCM)"))
                return nullptr;

            ComPtr<IMFMediaType> actual;
            if (FAILED(reader->GetCurrentMediaType(audioStream, &actual)))
                return nullptr;

            WAVEFORMATEX* waveFormat = nullptr;
            UINT32 formatSize = 0;
            if (FAILED(MFCreateWaveFormatExFromMFMediaType(actual.Get(), &waveFormat, &formatSize)))
                return nullptr;

            auto sound = std::make_shared<SoundData>();
            sound->format.assign(reinterpret_cast<uint8_t*>(waveFormat), reinterpret_cast<uint8_t*>(waveFormat) + formatSize);
            CoTaskMemFree(waveFormat);

            for (;;)
            {
                DWORD flags = 0;
                ComPtr<IMFSample> sample;
                if (FAILED(reader->ReadSample(audioStream, 0, nullptr, &flags, nullptr, &sample)))
                    break;
                if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
                    break;
                if (!sample)
                    continue;
                ComPtr<IMFMediaBuffer> buffer;
                if (FAILED(sample->ConvertToContiguousBuffer(&buffer)))
                    continue;
                BYTE* data = nullptr;
                DWORD length = 0;
                if (SUCCEEDED(buffer->Lock(&data, nullptr, &length)))
                {
                    sound->samples.insert(sound->samples.end(), data, data + length);
                    buffer->Unlock();
                }
            }

            // Keep whole sample frames only.
            const WAVEFORMATEX* fmt = sound->Format();
            if (fmt->nBlockAlign > 0)
                sound->samples.resize(sound->samples.size() - sound->samples.size() % fmt->nBlockAlign);
            if (sound->samples.empty())
                return nullptr;
            return sound;
        }
    }

    Audio::~Audio()
    {
        Shutdown();
    }

    bool Audio::Initialize()
    {
        m_mfStarted = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
        if (!Check(XAudio2Create(&m_xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR), "XAudio2Create"))
            return false;
        if (!Check(m_xaudio->CreateMasteringVoice(&m_master), "CreateMasteringVoice"))
        {
            m_xaudio.Reset();
            return false;
        }
        XAUDIO2_VOICE_DETAILS details{};
        m_master->GetVoiceDetails(&details);
        constexpr UINT32 channels = 2;
        Check(m_xaudio->CreateSubmixVoice(&m_effectsBus, channels, details.InputSampleRate), "CreateSubmixVoice(effects)");
        Check(m_xaudio->CreateSubmixVoice(&m_musicBus, channels, details.InputSampleRate), "CreateSubmixVoice(music)");
        return true;
    }

    void Audio::Shutdown()
    {
        auto destroy = [](ActiveVoice& v)
        {
            if (v.voice)
            {
                v.voice->Stop();
                v.voice->DestroyVoice();
                v.voice = nullptr;
            }
        };
        for (auto& v : m_oneShots)
            destroy(v);
        m_oneShots.clear();
        for (auto& [handle, v] : m_loops)
            destroy(v);
        m_loops.clear();
        destroy(m_music);
        if (m_effectsBus)
        {
            m_effectsBus->DestroyVoice();
            m_effectsBus = nullptr;
        }
        if (m_musicBus)
        {
            m_musicBus->DestroyVoice();
            m_musicBus = nullptr;
        }
        if (m_master)
        {
            m_master->DestroyVoice();
            m_master = nullptr;
        }
        if (m_xaudio)
        {
            m_xaudio->StopEngine();
            m_xaudio.Reset();
        }
        m_cache.clear();
        if (m_mfStarted)
        {
            MFShutdown();
            m_mfStarted = false;
        }
    }

    void Audio::Update()
    {
        // Reclaim finished one-shot voices.
        std::erase_if(m_oneShots, [](ActiveVoice& v)
        {
            XAUDIO2_VOICE_STATE state{};
            v.voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            if (state.BuffersQueued == 0)
            {
                v.voice->DestroyVoice();
                return true;
            }
            return false;
        });
    }

    std::shared_ptr<SoundData> Audio::Load(std::string_view relativePath)
    {
        if (relativePath.empty() || !m_xaudio)
            return nullptr;
        std::string key(relativePath);
        if (auto it = m_cache.find(key); it != m_cache.end())
            return it->second;

        auto sound = DecodeWithMediaFoundation(Paths::Asset(relativePath));
        if (!sound)
            Log("[PlutoFall] Could not load sound '{}'", key);
        m_cache[key] = sound; // cache failures too so we don't retry every frame
        return sound;
    }

    IXAudio2SourceVoice* Audio::CreateVoice(const SoundData& sound, Bus bus)
    {
        IXAudio2SubmixVoice* target = bus == Bus::Music ? m_musicBus : m_effectsBus;
        XAUDIO2_SEND_DESCRIPTOR send{ 0, target };
        XAUDIO2_VOICE_SENDS sends{ 1, &send };
        IXAudio2SourceVoice* voice = nullptr;
        if (FAILED(m_xaudio->CreateSourceVoice(&voice, sound.Format(), 0, 4.0f, nullptr, target ? &sends : nullptr)))
            return nullptr;
        return voice;
    }

    void Audio::PlaySfx(std::string_view relativePath, float volume, float pitch)
    {
        auto sound = Load(relativePath);
        if (!sound || m_oneShots.size() >= MaxOneShotVoices)
            return;

        // Avoid stacking the identical sound many times in the same instant (e.g. 4 players firing).
        const ULONGLONG now = GetTickCount64();
        auto& last = m_lastPlayed[std::string(relativePath)];
        if (now - last < 25)
            return;
        last = now;

        IXAudio2SourceVoice* voice = CreateVoice(*sound, Bus::Effects);
        if (!voice)
            return;
        XAUDIO2_BUFFER buffer{};
        buffer.AudioBytes = static_cast<UINT32>(sound->samples.size());
        buffer.pAudioData = sound->samples.data();
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        voice->SetVolume(volume);
        voice->SetFrequencyRatio(std::clamp(pitch, 0.25f, 4.0f));
        if (FAILED(voice->SubmitSourceBuffer(&buffer)) || FAILED(voice->Start()))
        {
            voice->DestroyVoice();
            return;
        }
        m_oneShots.push_back({ voice, std::move(sound) });
    }

    int Audio::StartLoop(std::string_view relativePath, Bus bus, float volume)
    {
        auto sound = Load(relativePath);
        if (!sound)
            return 0;
        IXAudio2SourceVoice* voice = CreateVoice(*sound, bus);
        if (!voice)
            return 0;
        XAUDIO2_BUFFER buffer{};
        buffer.AudioBytes = static_cast<UINT32>(sound->samples.size());
        buffer.pAudioData = sound->samples.data();
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
        voice->SetVolume(volume);
        if (FAILED(voice->SubmitSourceBuffer(&buffer)) || FAILED(voice->Start()))
        {
            voice->DestroyVoice();
            return 0;
        }
        const int handle = m_nextLoopHandle++;
        m_loops[handle] = { voice, std::move(sound) };
        return handle;
    }

    void Audio::StopLoop(int handle)
    {
        auto it = m_loops.find(handle);
        if (it == m_loops.end())
            return;
        it->second.voice->Stop();
        it->second.voice->FlushSourceBuffers();
        it->second.voice->DestroyVoice();
        m_loops.erase(it);
    }

    void Audio::PlayMusic(std::string_view relativePath)
    {
        if (relativePath.empty() || relativePath == m_musicName)
            return;
        StopMusic();
        m_musicName = relativePath;
        auto sound = Load(relativePath);
        if (!sound)
            return;
        IXAudio2SourceVoice* voice = CreateVoice(*sound, Bus::Music);
        if (!voice)
            return;
        XAUDIO2_BUFFER buffer{};
        buffer.AudioBytes = static_cast<UINT32>(sound->samples.size());
        buffer.pAudioData = sound->samples.data();
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
        if (FAILED(voice->SubmitSourceBuffer(&buffer)) || FAILED(voice->Start()))
        {
            voice->DestroyVoice();
            return;
        }
        m_music = { voice, std::move(sound) };
    }

    void Audio::StopMusic()
    {
        if (m_music.voice)
        {
            m_music.voice->Stop();
            m_music.voice->FlushSourceBuffers();
            m_music.voice->DestroyVoice();
        }
        m_music = {};
        m_musicName.clear();
    }

    void Audio::SetEffectsVolume(float volume)
    {
        if (m_effectsBus)
            m_effectsBus->SetVolume(Clamp01(volume) * Clamp01(volume));
    }

    void Audio::SetMusicVolume(float volume)
    {
        if (m_musicBus)
            m_musicBus->SetVolume(Clamp01(volume) * Clamp01(volume));
    }
}
