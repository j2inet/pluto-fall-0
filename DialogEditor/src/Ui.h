// Ui.h - small Win32 UI toolkit for the dialog editor: DPI scaling, fonts, control helpers,
// icon buttons, image thumbnails and modal pop-ups.
#pragma once
#include "Common.h"

namespace de::ui
{
    // Glyphs from Segoe Fluent Icons / Segoe MDL2 Assets.
    namespace Glyph
    {
        constexpr wchar_t Add = 0xE710;
        constexpr wchar_t Remove = 0xE738;
        constexpr wchar_t Delete = 0xE74D; // trash can
        constexpr wchar_t Save = 0xE74E;
        constexpr wchar_t Up = 0xE74A;
        constexpr wchar_t Down = 0xE74B;
        constexpr wchar_t Image = 0xEB9F;
        constexpr wchar_t Refresh = 0xE72C;
    }

    // Must be called once before any window is created.
    void Initialize(HINSTANCE instance);
    HINSTANCE Instance();

    // DPI handling. All layout constants are written for 96 DPI and passed through Scale().
    void SetDpi(UINT dpi);
    UINT Dpi();
    int Scale(int value);

    HFONT Font();        // standard UI font
    HFONT BoldFont();    // section headings
    HFONT SmallFont();   // secondary text
    HFONT IconFont();    // glyphs

    // Sends WM_SETFONT to every child of a window (used after a DPI change).
    void ApplyFonts(HWND root);

    // Control factories. All controls get the UI font.
    HWND Label(HWND parent, std::wstring_view text, HFONT font = nullptr);
    HWND Button(HWND parent, std::wstring_view text, int id);
    HWND IconButton(HWND parent, wchar_t glyph, std::wstring_view text, int id); // owner-drawn
    HWND CheckBox(HWND parent, std::wstring_view text, int id);
    HWND Edit(HWND parent, int id, bool multiline = false);
    HWND ComboList(HWND parent, int id);  // drop-down list (not editable)
    HWND ComboEdit(HWND parent, int id);  // editable drop-down
    HWND ListBox(HWND parent, int id);
    HWND ListView(HWND parent, int id, std::initializer_list<std::pair<const wchar_t*, int>> columns);
    HWND Thumbnail(HWND parent, int id);

    std::wstring GetText(HWND control);
    void SetText(HWND control, std::wstring_view text);

    // Draws an owner-drawn IconButton. Call from WM_DRAWITEM; returns false if not an icon button.
    bool DrawIconButton(const DRAWITEMSTRUCT& dis);

    // Thumbnail control: shows an image scaled to fit, or "(none)". Clicking it sends
    // WM_COMMAND with notification code STN_CLICKED to the parent.
    void SetThumbnail(HWND thumbnail, const fs::path& file, bool hasValue);

    // ListView helpers.
    void ListViewSetRows(HWND list, const std::vector<std::vector<std::wstring>>& rows, int selected);
    int ListViewSelection(HWND list);
    void ListViewSelect(HWND list, int index);

    // Modal pop-ups. `owner` is disabled while they are open.
    using Validator = std::function<std::wstring(std::wstring&)>; // returns an error message, or empty if OK
    std::optional<std::wstring> Prompt(HWND owner, std::wstring_view title, std::wstring_view message,
                                       std::wstring_view initial, const Validator& validate);
    void ShowImageFullScreen(HWND owner, const fs::path& file);
    // Lets the user pick an image under <assets>/images. Returns the path relative to the
    // assets folder with forward slashes (e.g. "images/bg_bridge.png").
    std::optional<std::string> PickImage(HWND owner, const fs::path& assets, std::string_view current);

    // Loads an image with WIC, scaled to fit inside maxWidth x maxHeight (keeping its aspect ratio),
    // as a 32-bit premultiplied DIB. Returns nullptr on failure.
    HBITMAP LoadImageFit(const fs::path& file, int maxWidth, int maxHeight, bool allowUpscale, SIZE* size);
    void DrawBitmap(HDC hdc, HBITMAP bitmap, SIZE size, const RECT& area); // centred, alpha-blended
}
