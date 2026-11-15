#include "Game.h"
#include "FireFunctions.h"
#include "Localization.h"
#include "MotionFunctions.h"
#include "Screens.h"
#include "Util.h"
#include <shellapi.h>

namespace pf
{
    namespace
    {
        constexpr float FadeDuration = 0.35f;
    }

    bool Game::Initialize(HWND hwnd)
    {
        m_hwnd = hwnd;
        QueryPerformanceFrequency(&m_frequency);
        QueryPerformanceCounter(&m_lastTime);

        Paths::Initialize();
        MotionRegistry::RegisterBuiltIns();
        FireRegistry::RegisterBuiltIns();

        m_settings.Load();
        Localization::Load();
        Localization::SetCurrent(m_settings.language.empty() ? Localization::DetectSystemLanguage() : m_settings.language);

        if (!m_graphics.Initialize(hwnd))
        {
            MessageBoxW(hwnd, L"Unable to initialise Direct3D 11.", L"Pluto Fall", MB_ICONERROR);
            return false;
        }
        if (!m_renderer.Initialize(m_graphics))
        {
            MessageBoxW(hwnd, L"Unable to compile shaders.", L"Pluto Fall", MB_ICONERROR);
            return false;
        }
        m_textures.Initialize(m_graphics);
        m_models.Initialize(m_graphics, m_textures);
        m_ui.Initialize(m_graphics);

        if (!m_audio.Initialize())
            Log("[PlutoFall] Audio unavailable; continuing without sound");
        ApplySettings();

        if (!m_data.Load())
            Log("[PlutoFall] data/game.json missing or invalid; using defaults");

        if (m_settings.fullscreen)
            ToggleFullscreen();

        m_screen = MakeTitleScreen(*this);
        m_screen->Enter();
        m_fadeIn = 1.0f;
        return true;
    }

    void Game::Shutdown()
    {
        if (m_screen)
            m_screen->Exit();
        m_screen.reset();
        m_pending.reset();
        m_audio.Shutdown();
        m_settings.fullscreen = m_fullscreen;
        m_settings.Save();
    }

    void Game::ApplySettings()
    {
        m_audio.SetEffectsVolume(m_settings.sfxVolume);
        m_audio.SetMusicVolume(m_settings.musicVolume);
        m_ui.ResetTextFormats();
    }

    void Game::OnResize(UINT width, UINT height)
    {
        m_graphics.Resize(width, height);
    }

    void Game::OnActivate(bool active)
    {
        m_active = active;
        if (!active)
            m_input.OnFocusLost();
    }

    void Game::ChangeScreen(std::unique_ptr<Screen> next)
    {
        if (m_pending)
            return; // a transition is already under way
        m_pending = std::move(next);
        m_fadeOut = 0.0f;
    }

    void Game::ShowTitle() { ChangeScreen(MakeTitleScreen(*this)); }
    void Game::ShowOptions() { ChangeScreen(MakeOptionsScreen(*this)); }
    void Game::ShowCredits() { ChangeScreen(MakeCreditsScreen(*this)); }
    void Game::ShowPlayerSelect() { ChangeScreen(MakePlayerSelectScreen(*this)); }
    void Game::ShowDialog(const std::string& dialogPath) { ChangeScreen(MakeDialogScreen(*this, dialogPath)); }
    void Game::ShowLevel(const std::string& levelPath) { ChangeScreen(MakeGameplayScreen(*this, levelPath)); }

    void Game::OpenWebsite()
    {
        const std::wstring url = Widen(m_data.Config().website);
        ShellExecuteW(m_hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void Game::Quit()
    {
        m_quit = true;
        PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
    }

    void Game::ToggleFullscreen()
    {
        // Borderless fullscreen window on the current monitor.
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(m_hwnd, GWL_STYLE));
        if (!m_fullscreen)
        {
            MONITORINFO mi{ sizeof(MONITORINFO) };
            if (GetWindowPlacement(m_hwnd, &m_windowPlacement) &&
                GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi))
            {
                SetWindowLongPtrW(m_hwnd, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
                SetWindowPos(m_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                             mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
                m_fullscreen = true;
            }
        }
        else
        {
            SetWindowLongPtrW(m_hwnd, GWL_STYLE, (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW);
            SetWindowPlacement(m_hwnd, &m_windowPlacement);
            SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            m_fullscreen = false;
        }
        m_settings.fullscreen = m_fullscreen;
    }

    void Game::Tick()
    {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        float dt = static_cast<float>(now.QuadPart - m_lastTime.QuadPart) / static_cast<float>(m_frequency.QuadPart);
        m_lastTime = now;
        dt = std::clamp(dt, 0.0f, 1.0f / 20.0f); // avoid huge steps after a stall
        m_totalTime += dt;

        m_input.Update(dt);
        m_audio.Update();

        if (m_pending)
        {
            // Fade out the current screen, then swap.
            m_fadeOut += dt / FadeDuration;
            if (m_fadeOut >= 1.0f)
            {
                m_screen->Exit();
                m_screen = std::move(m_pending);
                m_screen->Enter();
                m_fadeOut = 0.0f;
                m_fadeIn = 1.0f;
            }
        }
        else
        {
            if (m_fadeIn > 0.0f)
                m_fadeIn = std::max(0.0f, m_fadeIn - dt / FadeDuration);
            m_screen->Update(dt);
        }

        if (m_graphics.IsMinimized())
            return;

        m_graphics.BeginFrame(Colors::Black);
        m_screen->Render();

        const float fade = m_pending ? Clamp01(m_fadeOut) : Clamp01(m_fadeIn);
        if (fade > 0.0f)
        {
            m_graphics.Begin2D();
            m_ui.FillScreen(Color{ 0, 0, 0, fade });
            m_graphics.End2D();
        }
        m_graphics.Present();
    }
}
