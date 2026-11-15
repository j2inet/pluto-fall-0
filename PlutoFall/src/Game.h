// Game.h - owns every subsystem and the active screen.
#pragma once
#include "Common.h"
#include "Audio.h"
#include "GameData.h"
#include "Graphics.h"
#include "Input.h"
#include "Model.h"
#include "Renderer3D.h"
#include "Settings.h"
#include "Texture.h"
#include "Ui.h"

namespace pf
{
    class Screen
    {
    public:
        explicit Screen(class Game& game) : m_game(game) {}
        virtual ~Screen() = default;
        Screen(const Screen&) = delete;
        Screen& operator=(const Screen&) = delete;

        virtual void Enter() {}
        virtual void Exit() {}
        virtual void Update(float dt) = 0;
        virtual void Render() = 0;

    protected:
        class Game& m_game;
    };

    class Game
    {
    public:
        bool Initialize(HWND hwnd);
        void Shutdown();
        void Tick();

        void OnResize(UINT width, UINT height);
        void OnKey(WPARAM key, bool down) { m_input.OnKey(key, down); }
        void OnActivate(bool active);

        // Screen navigation. The switch happens after a short fade.
        void ShowTitle();
        void ShowOptions();
        void ShowCredits();
        void ShowPlayerSelect();
        void ShowDialog(const std::string& dialogPath);
        void ShowLevel(const std::string& levelPath);
        void OpenWebsite();
        void Quit();

        bool IsTransitioning() const noexcept { return m_pending != nullptr || m_fadeIn > 0.0f; }
        bool QuitRequested() const noexcept { return m_quit; }
        void ToggleFullscreen();

        // Re-applies volume/language settings after the options screen changes them.
        void ApplySettings();

        Graphics& Gfx() noexcept { return m_graphics; }
        Renderer3D& Renderer() noexcept { return m_renderer; }
        Ui& UI() noexcept { return m_ui; }
        Audio& Sound() noexcept { return m_audio; }
        Input& Controls() noexcept { return m_input; }
        Settings& Options() noexcept { return m_settings; }
        GameData& Data() noexcept { return m_data; }
        ModelCache& Models() noexcept { return m_models; }
        Session& CurrentSession() noexcept { return m_session; }
        float TotalTime() const noexcept { return m_totalTime; }

        // Helpers for common sounds.
        void PlayMenuMove() { m_audio.PlaySfx(m_data.Config().sounds.menuMove, 0.7f); }
        void PlayMenuSelect() { m_audio.PlaySfx(m_data.Config().sounds.menuSelect, 0.9f); }

    private:
        void ChangeScreen(std::unique_ptr<Screen> next);

        HWND m_hwnd = nullptr;
        Graphics m_graphics;
        Renderer3D m_renderer;
        TextureCache m_textures;
        ModelCache m_models;
        Ui m_ui;
        Audio m_audio;
        Input m_input;
        Settings m_settings;
        GameData m_data;
        Session m_session;

        std::unique_ptr<Screen> m_screen;
        std::unique_ptr<Screen> m_pending;
        float m_fadeOut = 0.0f; // 0..1 while fading out to the pending screen
        float m_fadeIn = 0.0f;  // 1..0 while fading in the new screen

        LARGE_INTEGER m_frequency{};
        LARGE_INTEGER m_lastTime{};
        float m_totalTime = 0.0f;
        bool m_quit = false;
        bool m_active = true;

        // Windowed placement saved while fullscreen.
        WINDOWPLACEMENT m_windowPlacement{ sizeof(WINDOWPLACEMENT) };
        bool m_fullscreen = false;
    };
}
