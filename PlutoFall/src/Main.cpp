// Main.cpp - Win32 entry point and message loop for Pluto Fall.
#include "Game.h"
#include "Util.h"

#if defined(_MSC_VER)
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#endif

namespace
{
    pf::Game* g_game = nullptr;

    LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_SIZE:
            if (g_game)
                g_game->OnResize(LOWORD(lParam), HIWORD(lParam));
            return 0;

        case WM_ACTIVATEAPP:
            if (g_game)
                g_game->OnActivate(wParam != FALSE);
            return 0;

        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            // Alt+Enter or F11 toggles borderless fullscreen.
            if ((wParam == VK_RETURN && (HIWORD(lParam) & KF_ALTDOWN)) || wParam == VK_F11)
            {
                if (g_game && !(HIWORD(lParam) & KF_REPEAT))
                    g_game->ToggleFullscreen();
                return 0;
            }
            if (g_game)
                g_game->OnKey(wParam, true);
            if (message == WM_SYSKEYDOWN && wParam != VK_F4)
                return 0; // swallow F10/Alt so the window menu doesn't steal focus
            break;

        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (g_game)
                g_game->OnKey(wParam, false);
            if (message == WM_SYSKEYUP)
                return 0;
            break;

        case WM_MENUCHAR:
            // Prevent the beep on Alt+Enter.
            return MAKELRESULT(0, MNC_CLOSE);

        case WM_GETMINMAXINFO:
        {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize = { 640, 360 };
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int showCommand)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        return 1;

    WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = L"PlutoFallWindow";
    RegisterClassExW(&wc);

    // 1600x900 client area, centred on the primary monitor.
    RECT rect{ 0, 0, 1600, 900 };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int x = std::max(0, (GetSystemMetrics(SM_CXSCREEN) - width) / 2);
    const int y = std::max(0, (GetSystemMetrics(SM_CYSCREEN) - height) / 2);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Pluto Fall", WS_OVERLAPPEDWINDOW, x, y, width, height, nullptr,
                                nullptr, instance, nullptr);
    if (!hwnd)
    {
        CoUninitialize();
        return 1;
    }

    int exitCode = 0;
    {
        pf::Game game;
        g_game = &game;
        ShowWindow(hwnd, showCommand);
        UpdateWindow(hwnd);

        if (game.Initialize(hwnd))
        {
            MSG msg{};
            while (msg.message != WM_QUIT)
            {
                if (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                else
                {
                    game.Tick();
                }
            }
            exitCode = static_cast<int>(msg.wParam);
        }
        else
        {
            exitCode = 1;
            DestroyWindow(hwnd);
        }
        game.Shutdown();
        g_game = nullptr;
    }

    CoUninitialize();
    return exitCode;
}
