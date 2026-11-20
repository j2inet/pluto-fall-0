// Main.cpp - entry point for the Pluto Fall dialogue editor.
//
// Usage:  DialogEditor.exe [/asset "C:\full\path\to\assets"]
// Without /asset, the editor looks for an "assets" folder next to the executable and then in
// each parent folder in turn.
#include "MainWindow.h"
#include "Ui.h"
#include "Util.h"

#if defined(_MSC_VER)
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
// Use version 6 of the common controls (visual styles).
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int show)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
        return 1;
    de::ui::Initialize(instance);

    std::wstring error;
    std::optional<de::fs::path> assets = de::FindAssetsFolder(error);
    if (!assets)
    {
        if (!error.empty())
        {
            MessageBoxW(nullptr, error.c_str(), L"Pluto Fall Dialogue Editor", MB_ICONERROR);
            CoUninitialize();
            return 1;
        }
        const int answer = MessageBoxW(nullptr,
                                       L"Could not find an \"assets\" folder next to the editor or in any of its parent "
                                       L"folders.\n\nWould you like to choose the assets folder now?\n\n"
                                       L"(You can also start the editor with:  /asset \"C:\\path\\to\\assets\")",
                                       L"Pluto Fall Dialogue Editor", MB_YESNO | MB_ICONQUESTION);
        if (answer == IDYES)
            assets = de::BrowseForFolder(nullptr, L"Choose the Pluto Fall assets folder");
        if (!assets)
        {
            CoUninitialize();
            return 1;
        }
    }

    int exitCode = 0;
    {
        de::MainWindow window;
        if (!window.Create(instance, *assets, show))
        {
            CoUninitialize();
            return 1;
        }

        ACCEL keys[] = {
            { FCONTROL | FVIRTKEY, 'S', static_cast<WORD>(de::CommandId::Save) },
            { FCONTROL | FVIRTKEY, 'W', static_cast<WORD>(de::CommandId::CloseTab) },
            { FCONTROL | FVIRTKEY, 'N', static_cast<WORD>(de::CommandId::NewDialog) },
            { FVIRTKEY, VK_F5, static_cast<WORD>(de::CommandId::Refresh) },
        };
        HACCEL accelerators = CreateAcceleratorTableW(keys, static_cast<int>(std::size(keys)));

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            if (TranslateAcceleratorW(window.Hwnd(), accelerators, &msg))
                continue;
            if (IsDialogMessageW(window.Hwnd(), &msg))
            {
                // Tab / arrow navigation may have moved focus off-screen in a scrolled editor.
                if (msg.message == WM_KEYDOWN)
                    window.AfterKeyboardNavigation();
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        exitCode = static_cast<int>(msg.wParam);
        DestroyAcceleratorTable(accelerators);
    }
    CoUninitialize();
    return exitCode;
}
