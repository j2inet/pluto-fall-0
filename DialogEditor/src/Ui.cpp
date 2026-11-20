#include "Ui.h"
#include "Util.h"

namespace de::ui
{
    namespace
    {
        HINSTANCE g_instance = nullptr;
        UINT g_dpi = 96;
        HFONT g_font = nullptr;
        HFONT g_bold = nullptr;
        HFONT g_small = nullptr;
        HFONT g_icon = nullptr;
        std::wstring g_iconFace;

        constexpr wchar_t ThumbnailClass[] = L"DE_Thumbnail";
        constexpr wchar_t PromptClass[] = L"DE_Prompt";
        constexpr wchar_t ViewerClass[] = L"DE_Viewer";
        constexpr wchar_t PickerClass[] = L"DE_Picker";
        constexpr UINT WM_SETTHUMBNAIL = WM_APP + 1;

        bool FontExists(const wchar_t* face)
        {
            HDC hdc = GetDC(nullptr);
            LOGFONTW lf{};
            lf.lfCharSet = DEFAULT_CHARSET;
            wcsncpy_s(lf.lfFaceName, face, _TRUNCATE);
            bool found = false;
            EnumFontFamiliesExW(
                hdc, &lf, [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM param) -> int
                {
                    *reinterpret_cast<bool*>(param) = true;
                    return 0;
                },
                reinterpret_cast<LPARAM>(&found), 0);
            ReleaseDC(nullptr, hdc);
            return found;
        }

        void CreateFonts()
        {
            for (HFONT f : { g_font, g_bold, g_small, g_icon })
                if (f)
                    DeleteObject(f);

            NONCLIENTMETRICSW ncm{};
            ncm.cbSize = sizeof(ncm);
            SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, g_dpi);
            LOGFONTW lf = ncm.lfMessageFont;
            g_font = CreateFontIndirectW(&lf);

            LOGFONTW bold = lf;
            bold.lfWeight = FW_SEMIBOLD;
            bold.lfHeight = MulDiv(lf.lfHeight, 115, 100);
            g_bold = CreateFontIndirectW(&bold);

            LOGFONTW smallFont = lf;
            smallFont.lfHeight = MulDiv(lf.lfHeight, 85, 100);
            g_small = CreateFontIndirectW(&smallFont);

            LOGFONTW icon{};
            icon.lfHeight = MulDiv(lf.lfHeight, 110, 100);
            icon.lfWeight = FW_NORMAL;
            icon.lfCharSet = DEFAULT_CHARSET;
            icon.lfQuality = CLEARTYPE_QUALITY;
            wcsncpy_s(icon.lfFaceName, g_iconFace.c_str(), _TRUNCATE);
            g_icon = CreateFontIndirectW(&icon);
        }

        COLORREF GlyphColor(wchar_t glyph)
        {
            switch (glyph)
            {
            case Glyph::Add: return RGB(16, 124, 16);
            case Glyph::Remove:
            case Glyph::Delete: return RGB(196, 43, 28);
            case Glyph::Save: return RGB(0, 95, 184);
            default: return GetSysColor(COLOR_BTNTEXT);
            }
        }

        // ---------------------------------------------------------------- modal loop

        void RunModal(HWND owner, HWND popup, const bool& done, HWND focus = nullptr)
        {
            if (owner)
                EnableWindow(owner, FALSE);
            ShowWindow(popup, SW_SHOW);
            SetForegroundWindow(popup);
            SetFocus(focus ? focus : popup);
            MSG msg{};
            bool quit = false;
            while (!done)
            {
                const BOOL got = GetMessageW(&msg, nullptr, 0, 0);
                if (got == 0)
                {
                    quit = true;
                    break;
                }
                if (got == -1)
                    break;
                if (!IsDialogMessageW(popup, &msg))
                {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
            if (owner)
            {
                EnableWindow(owner, TRUE);
                SetActiveWindow(owner);
            }
            DestroyWindow(popup);
            if (quit)
                PostQuitMessage(static_cast<int>(msg.wParam));
        }

        RECT CenteredOn(HWND owner, int width, int height)
        {
            RECT area{};
            if (owner && IsWindowVisible(owner))
                GetWindowRect(owner, &area);
            else
                SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
            const int x = area.left + (area.right - area.left - width) / 2;
            const int y = area.top + (area.bottom - area.top - height) / 2;
            return { x, y, x + width, y + height };
        }

        LRESULT StaticColor(HDC hdc, HWND control, int background)
        {
            SetBkColor(hdc, GetSysColor(background));
            if (GetPropW(control, L"de.error"))
                SetTextColor(hdc, RGB(196, 43, 28));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(background));
        }

        // ---------------------------------------------------------------- thumbnail control

        struct ThumbState
        {
            fs::path file;
            bool hasValue = false;
            HBITMAP bitmap = nullptr;
            SIZE size{};
            SIZE loadedFor{};
            bool failed = false;

            void Reset()
            {
                if (bitmap)
                    DeleteObject(bitmap);
                bitmap = nullptr;
                failed = false;
                loadedFor = {};
            }
        };

        LRESULT CALLBACK ThumbnailProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
        {
            auto* state = reinterpret_cast<ThumbState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            switch (msg)
            {
            case WM_NCCREATE:
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new ThumbState));
                break;
            case WM_NCDESTROY:
                if (state)
                {
                    state->Reset();
                    delete state;
                }
                break;
            case WM_SETTHUMBNAIL:
            {
                const auto* file = reinterpret_cast<const fs::path*>(lp);
                state->Reset();
                state->file = *file;
                state->hasValue = wp != 0;
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
            case WM_SIZE:
                if (state && state->bitmap)
                    state->Reset();
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT:
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT rc;
                GetClientRect(hwnd, &rc);
                HBRUSH back = CreateSolidBrush(RGB(36, 40, 48));
                FillRect(hdc, &rc, back);
                DeleteObject(back);
                RECT inner = rc;
                InflateRect(&inner, -2, -2);
                const int w = inner.right - inner.left, h = inner.bottom - inner.top;
                if (state->hasValue && w > 0 && h > 0)
                {
                    if (!state->bitmap && !state->failed)
                    {
                        state->bitmap = LoadImageFit(state->file, w, h, true, &state->size);
                        state->failed = state->bitmap == nullptr;
                    }
                }
                SetBkMode(hdc, TRANSPARENT);
                HGDIOBJ oldFont = SelectObject(hdc, g_small);
                if (state->hasValue && state->bitmap)
                {
                    DrawBitmap(hdc, state->bitmap, state->size, inner);
                }
                else
                {
                    SetTextColor(hdc, state->hasValue ? RGB(255, 120, 100) : RGB(160, 165, 175));
                    const wchar_t* text = state->hasValue ? L"Image not found" : L"(none)";
                    DrawTextW(hdc, text, -1, &inner, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                SelectObject(hdc, oldFont);
                HBRUSH frame = GetSysColorBrush(COLOR_3DSHADOW);
                FrameRect(hdc, &rc, frame);
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_SETCURSOR:
                if (state && state->hasValue && state->bitmap)
                {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
                break;
            case WM_LBUTTONUP:
                SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(hwnd), STN_CLICKED),
                             reinterpret_cast<LPARAM>(hwnd));
                return 0;
            default:
                break;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        // ---------------------------------------------------------------- prompt

        struct PromptState
        {
            std::wstring message;
            std::wstring initial;
            const Validator* validate = nullptr;
            std::optional<std::wstring> result;
            bool done = false;
            HWND edit = nullptr;
            HWND error = nullptr;
        };

        LRESULT CALLBACK PromptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
        {
            auto* state = reinterpret_cast<PromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            switch (msg)
            {
            case WM_NCCREATE:
                SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                                  reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
                break;
            case WM_CREATE:
            {
                RECT rc;
                GetClientRect(hwnd, &rc);
                const int m = Scale(14), w = rc.right - 2 * m;
                HWND label = Label(hwnd, state->message);
                MoveWindow(label, m, m, w, Scale(36), FALSE);
                state->edit = Edit(hwnd, 100);
                MoveWindow(state->edit, m, m + Scale(40), w, Scale(26), FALSE);
                SetText(state->edit, state->initial);
                SendMessageW(state->edit, EM_SETSEL, 0, -1);
                state->error = Label(hwnd, L"");
                SetPropW(state->error, L"de.error", reinterpret_cast<HANDLE>(1));
                MoveWindow(state->error, m, m + Scale(72), w, Scale(36), FALSE);
                HWND ok = Button(hwnd, L"OK", IDOK);
                SetWindowLongPtrW(ok, GWL_STYLE, GetWindowLongPtrW(ok, GWL_STYLE) | BS_DEFPUSHBUTTON);
                HWND cancel = Button(hwnd, L"Cancel", IDCANCEL);
                const int bw = Scale(88), bh = Scale(28), by = rc.bottom - m - bh;
                MoveWindow(ok, rc.right - m - 2 * bw - Scale(8), by, bw, bh, FALSE);
                MoveWindow(cancel, rc.right - m - bw, by, bw, bh, FALSE);
                SetFocus(state->edit);
                return 0;
            }
            case DM_GETDEFID:
                return MAKELRESULT(IDOK, DC_HASDEFID);
            case WM_CTLCOLORSTATIC:
                return StaticColor(reinterpret_cast<HDC>(wp), reinterpret_cast<HWND>(lp), COLOR_BTNFACE);
            case WM_COMMAND:
                if (LOWORD(wp) == IDOK)
                {
                    std::wstring value = GetText(state->edit);
                    const std::wstring problem = state->validate && *state->validate ? (*state->validate)(value) : std::wstring();
                    if (!problem.empty())
                    {
                        SetText(state->error, problem);
                        SetFocus(state->edit);
                        SendMessageW(state->edit, EM_SETSEL, 0, -1);
                        return 0;
                    }
                    state->result = value;
                    state->done = true;
                    return 0;
                }
                if (LOWORD(wp) == IDCANCEL)
                {
                    state->done = true;
                    return 0;
                }
                break;
            case WM_CLOSE:
                state->done = true;
                return 0;
            default:
                break;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        // ---------------------------------------------------------------- full screen viewer

        struct ViewerState
        {
            fs::path file;
            HBITMAP bitmap = nullptr;
            SIZE size{};
            bool done = false;
        };

        LRESULT CALLBACK ViewerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
        {
            auto* state = reinterpret_cast<ViewerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            switch (msg)
            {
            case WM_NCCREATE:
                SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                                  reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
                break;
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT:
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT rc;
                GetClientRect(hwnd, &rc);
                FillRect(hdc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                if (state->bitmap)
                {
                    DrawBitmap(hdc, state->bitmap, state->size, rc);
                }
                else
                {
                    SetBkMode(hdc, TRANSPARENT);
                    SetTextColor(hdc, RGB(255, 255, 255));
                    HGDIOBJ old = SelectObject(hdc, g_font);
                    const std::wstring text = L"Could not load " + state->file.wstring();
                    DrawTextW(hdc, text.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    SelectObject(hdc, old);
                }
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_LBUTTONUP:
            case WM_RBUTTONUP:
            case WM_KEYDOWN:
            case WM_CLOSE:
                state->done = true;
                return 0;
            case WM_COMMAND:
                if (LOWORD(wp) == IDCANCEL || LOWORD(wp) == IDOK)
                    state->done = true;
                return 0;
            case WM_GETDLGCODE:
                return DLGC_WANTALLKEYS;
            default:
                break;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        // ---------------------------------------------------------------- image picker

        struct PickerState
        {
            fs::path assets;
            std::vector<fs::path> files;
            int initial = -1;
            std::optional<std::string> result;
            bool done = false;
            HWND list = nullptr;
            HWND ok = nullptr;
            HWND cancel = nullptr;
            HWND info = nullptr;
            HIMAGELIST images = nullptr;
        };

        HBITMAP MakeTile(const fs::path& file, int width, int height)
        {
            BITMAPINFO bi{};
            bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth = width;
            bi.bmiHeader.biHeight = -height;
            bi.bmiHeader.biPlanes = 1;
            bi.bmiHeader.biBitCount = 32;
            void* bits = nullptr;
            HBITMAP tile = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (!tile)
                return nullptr;
            auto* px = static_cast<uint32_t*>(bits);
            std::fill(px, px + static_cast<size_t>(width) * height, 0xFF30343Cu); // opaque dark grey
            SIZE size{};
            if (HBITMAP image = LoadImageFit(file, width - 4, height - 4, false, &size))
            {
                HDC screen = GetDC(nullptr);
                HDC dc = CreateCompatibleDC(screen);
                HGDIOBJ old = SelectObject(dc, tile);
                RECT area{ 0, 0, width, height };
                DrawBitmap(dc, image, size, area);
                SelectObject(dc, old);
                DeleteDC(dc);
                ReleaseDC(nullptr, screen);
                DeleteObject(image);
            }
            return tile;
        }

        void LayoutPicker(HWND hwnd, PickerState& s)
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            const int m = Scale(10), bh = Scale(30), bw = Scale(96);
            const int barTop = rc.bottom - m - bh;
            MoveWindow(s.list, m, m, rc.right - 2 * m, barTop - 2 * m, TRUE);
            MoveWindow(s.info, m, barTop + Scale(6), rc.right - 3 * m - 2 * bw - Scale(8), bh, TRUE);
            MoveWindow(s.ok, rc.right - m - 2 * bw - Scale(8), barTop, bw, bh, TRUE);
            MoveWindow(s.cancel, rc.right - m - bw, barTop, bw, bh, TRUE);
        }

        void PickerChoose(PickerState& s)
        {
            const int index = ListView_GetNextItem(s.list, -1, LVNI_SELECTED);
            if (index < 0 || index >= static_cast<int>(s.files.size()))
                return;
            s.result = Narrow(fs::relative(s.files[static_cast<size_t>(index)], s.assets).generic_wstring());
            s.done = true;
        }

        LRESULT CALLBACK PickerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
        {
            auto* s = reinterpret_cast<PickerState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            switch (msg)
            {
            case WM_NCCREATE:
                SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                                  reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
                break;
            case WM_CREATE:
            {
                s->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_ICON | LVS_SINGLESEL | LVS_SHOWSELALWAYS |
                                              LVS_AUTOARRANGE,
                                          0, 0, 10, 10, hwnd, reinterpret_cast<HMENU>(100), g_instance, nullptr);
                SendMessageW(s->list, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), FALSE);
                SetWindowTheme(s->list, L"Explorer", nullptr);
                ListView_SetExtendedListViewStyle(s->list, LVS_EX_DOUBLEBUFFER);
                s->ok = Button(hwnd, L"Select", IDOK);
                s->cancel = Button(hwnd, L"Cancel", IDCANCEL);
                s->info = Label(hwnd, L"");
                LayoutPicker(hwnd, *s);

                const int tw = Scale(176), th = Scale(99);
                s->images = ImageList_Create(tw, th, ILC_COLOR32, static_cast<int>(s->files.size()), 4);
                ListView_SetImageList(s->list, s->images, LVSIL_NORMAL);
                ListView_SetIconSpacing(s->list, tw + Scale(24), th + Scale(44));
                const fs::path imagesFolder = s->assets / L"images";
                HCURSOR oldCursor = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
                for (size_t i = 0; i < s->files.size(); ++i)
                {
                    HBITMAP tile = MakeTile(s->files[i], tw, th);
                    const int image = tile ? ImageList_Add(s->images, tile, nullptr) : -1;
                    if (tile)
                        DeleteObject(tile);
                    std::wstring name = fs::relative(s->files[i], imagesFolder).generic_wstring();
                    LVITEMW item{};
                    item.mask = LVIF_TEXT | LVIF_IMAGE;
                    item.iItem = static_cast<int>(i);
                    item.iImage = image;
                    item.pszText = name.data();
                    ListView_InsertItem(s->list, &item);
                }
                SetCursor(oldCursor);
                if (s->files.empty())
                    SetText(s->info, L"No images found in " + imagesFolder.wstring());
                else
                    SetText(s->info, L"Double-click an image, or select it and press Select.");
                if (s->initial >= 0)
                {
                    ListView_SetItemState(s->list, s->initial, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                    ListView_EnsureVisible(s->list, s->initial, FALSE);
                }
                EnableWindow(s->ok, s->initial >= 0);
                SetFocus(s->list);
                return 0;
            }
            case WM_SIZE:
                if (s && s->list)
                    LayoutPicker(hwnd, *s);
                return 0;
            case WM_GETMINMAXINFO:
                reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = { Scale(420), Scale(300) };
                return 0;
            case DM_GETDEFID:
                return MAKELRESULT(IDOK, DC_HASDEFID);
            case WM_CTLCOLORSTATIC:
                return StaticColor(reinterpret_cast<HDC>(wp), reinterpret_cast<HWND>(lp), COLOR_BTNFACE);
            case WM_NOTIFY:
            {
                const auto* hdr = reinterpret_cast<const NMHDR*>(lp);
                if (hdr->hwndFrom == s->list)
                {
                    if (hdr->code == LVN_ITEMACTIVATE)
                        PickerChoose(*s);
                    else if (hdr->code == LVN_ITEMCHANGED)
                        EnableWindow(s->ok, ListView_GetNextItem(s->list, -1, LVNI_SELECTED) >= 0);
                }
                break;
            }
            case WM_COMMAND:
                if (LOWORD(wp) == IDOK)
                {
                    PickerChoose(*s);
                    return 0;
                }
                if (LOWORD(wp) == IDCANCEL)
                {
                    s->done = true;
                    return 0;
                }
                break;
            case WM_CLOSE:
                s->done = true;
                return 0;
            case WM_DESTROY:
                if (s->images)
                    ImageList_Destroy(s->images);
                s->images = nullptr;
                break;
            default:
                break;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }

        void Register(const wchar_t* name, WNDPROC proc, int background, HCURSOR cursor = nullptr)
        {
            WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
            wc.lpfnWndProc = proc;
            wc.hInstance = g_instance;
            wc.hCursor = cursor ? cursor : LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = background >= 0 ? GetSysColorBrush(background) : nullptr;
            wc.lpszClassName = name;
            RegisterClassExW(&wc);
        }
    }

    // ------------------------------------------------------------------ setup

    void Initialize(HINSTANCE instance)
    {
        g_instance = instance;
        INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES };
        InitCommonControlsEx(&icc);
        g_iconFace = FontExists(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
        Register(ThumbnailClass, ThumbnailProc, -1);
        Register(PromptClass, PromptProc, COLOR_BTNFACE);
        Register(ViewerClass, ViewerProc, -1);
        Register(PickerClass, PickerProc, COLOR_BTNFACE);
        CreateFonts();
    }

    HINSTANCE Instance() { return g_instance; }

    void SetDpi(UINT dpi)
    {
        if (dpi == 0 || dpi == g_dpi)
            return;
        g_dpi = dpi;
        CreateFonts();
    }

    UINT Dpi() { return g_dpi; }
    int Scale(int value) { return MulDiv(value, static_cast<int>(g_dpi), 96); }
    HFONT Font() { return g_font; }
    HFONT BoldFont() { return g_bold; }
    HFONT SmallFont() { return g_small; }
    HFONT IconFont() { return g_icon; }

    void ApplyFonts(HWND root)
    {
        EnumChildWindows(
            root, [](HWND child, LPARAM) -> BOOL
            {
                // Keep each control's font role: headings and secondary text are tagged with a property.
                HFONT font = g_font;
                if (GetPropW(child, L"de.font.bold"))
                    font = g_bold;
                else if (GetPropW(child, L"de.font.small"))
                    font = g_small;
                SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                return TRUE;
            },
            0);
    }

    // ------------------------------------------------------------------ controls

    namespace
    {
        HWND Make(DWORD exStyle, const wchar_t* cls, std::wstring_view text, DWORD style, HWND parent, int id,
                  HFONT font = nullptr)
        {
            const std::wstring t(text);
            HWND hwnd = CreateWindowExW(exStyle, cls, t.c_str(), WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, parent,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_instance, nullptr);
            font = font ? font : g_font;
            SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
            if (font == g_bold)
                SetPropW(hwnd, L"de.font.bold", reinterpret_cast<HANDLE>(1));
            else if (font == g_small)
                SetPropW(hwnd, L"de.font.small", reinterpret_cast<HANDLE>(1));
            return hwnd;
        }
    }

    HWND Label(HWND parent, std::wstring_view text, HFONT font)
    {
        return Make(0, WC_STATICW, text, SS_LEFT | SS_NOPREFIX, parent, -1, font);
    }

    HWND Button(HWND parent, std::wstring_view text, int id)
    {
        return Make(0, WC_BUTTONW, text, WS_TABSTOP | BS_PUSHBUTTON, parent, id);
    }

    HWND IconButton(HWND parent, wchar_t glyph, std::wstring_view text, int id)
    {
        HWND hwnd = Make(0, WC_BUTTONW, text, WS_TABSTOP | BS_OWNERDRAW, parent, id);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, static_cast<LONG_PTR>(glyph));
        return hwnd;
    }

    HWND CheckBox(HWND parent, std::wstring_view text, int id)
    {
        return Make(0, WC_BUTTONW, text, WS_TABSTOP | BS_AUTOCHECKBOX, parent, id);
    }

    HWND Edit(HWND parent, int id, bool multiline)
    {
        DWORD style = WS_TABSTOP | ES_AUTOHSCROLL;
        if (multiline)
            style = WS_TABSTOP | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL;
        return Make(WS_EX_CLIENTEDGE, WC_EDITW, L"", style, parent, id);
    }

    HWND ComboList(HWND parent, int id)
    {
        return Make(0, WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, parent, id);
    }

    HWND ComboEdit(HWND parent, int id)
    {
        return Make(0, WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL, parent, id);
    }

    HWND ListBox(HWND parent, int id)
    {
        return Make(WS_EX_CLIENTEDGE, WC_LISTBOXW, L"", WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                    parent, id);
    }

    HWND ListView(HWND parent, int id, std::initializer_list<std::pair<const wchar_t*, int>> columns)
    {
        HWND hwnd = Make(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                         WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER, parent, id);
        ListView_SetExtendedListViewStyle(hwnd, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
        SetWindowTheme(hwnd, L"Explorer", nullptr);
        int index = 0;
        for (const auto& [title, width] : columns)
        {
            LVCOLUMNW col{};
            col.mask = LVCF_TEXT | LVCF_WIDTH;
            col.pszText = const_cast<wchar_t*>(title);
            col.cx = Scale(width);
            ListView_InsertColumn(hwnd, index++, &col);
        }
        return hwnd;
    }

    HWND Thumbnail(HWND parent, int id)
    {
        return Make(0, ThumbnailClass, L"", 0, parent, id);
    }

    std::wstring GetText(HWND control)
    {
        const int length = GetWindowTextLengthW(control);
        std::wstring text(static_cast<size_t>(length) + 1, L'\0');
        GetWindowTextW(control, text.data(), length + 1);
        text.resize(static_cast<size_t>(length));
        return text;
    }

    void SetText(HWND control, std::wstring_view text)
    {
        const std::wstring t(text);
        SetWindowTextW(control, t.c_str());
    }

    bool DrawIconButton(const DRAWITEMSTRUCT& dis)
    {
        if (dis.CtlType != ODT_BUTTON)
            return false;
        const auto glyph = static_cast<wchar_t>(GetWindowLongPtrW(dis.hwndItem, GWLP_USERDATA));
        HDC hdc = dis.hDC;
        RECT rc = dis.rcItem;
        const bool disabled = (dis.itemState & ODS_DISABLED) != 0;
        const bool pressed = (dis.itemState & ODS_SELECTED) != 0;

        if (HTHEME theme = OpenThemeData(dis.hwndItem, L"BUTTON"))
        {
            const int state = disabled ? PBS_DISABLED : pressed ? PBS_PRESSED : PBS_NORMAL;
            if (IsThemeBackgroundPartiallyTransparent(theme, BP_PUSHBUTTON, state))
                DrawThemeParentBackground(dis.hwndItem, hdc, &rc);
            DrawThemeBackground(theme, hdc, BP_PUSHBUTTON, state, &rc, nullptr);
            CloseThemeData(theme);
        }
        else
        {
            DrawFrameControl(hdc, &rc, DFC_BUTTON, DFCS_BUTTONPUSH | (pressed ? DFCS_PUSHED : 0));
        }

        const std::wstring text = GetText(dis.hwndItem);
        const std::wstring glyphText(1, glyph);
        SetBkMode(hdc, TRANSPARENT);

        HGDIOBJ old = SelectObject(hdc, g_icon);
        SIZE glyphSize{};
        GetTextExtentPoint32W(hdc, glyphText.c_str(), 1, &glyphSize);
        SelectObject(hdc, g_font);
        SIZE textSize{};
        GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.size()), &textSize);
        const int gap = text.empty() ? 0 : Scale(6);
        const int total = glyphSize.cx + gap + textSize.cx;
        int x = rc.left + (rc.right - rc.left - total) / 2;
        if (pressed)
            OffsetRect(&rc, 1, 1);

        SelectObject(hdc, g_icon);
        SetTextColor(hdc, disabled ? GetSysColor(COLOR_GRAYTEXT) : GlyphColor(glyph));
        RECT g{ x, rc.top, x + glyphSize.cx, rc.bottom };
        DrawTextW(hdc, glyphText.c_str(), 1, &g, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        x += glyphSize.cx + gap;

        SelectObject(hdc, g_font);
        SetTextColor(hdc, GetSysColor(disabled ? COLOR_GRAYTEXT : COLOR_BTNTEXT));
        RECT t{ x, rc.top, x + textSize.cx + 2, rc.bottom };
        DrawTextW(hdc, text.c_str(), static_cast<int>(text.size()), &t, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        SelectObject(hdc, old);

        if ((dis.itemState & ODS_FOCUS) && !(dis.itemState & ODS_NOFOCUSRECT))
        {
            RECT focus = dis.rcItem;
            InflateRect(&focus, -Scale(4), -Scale(4));
            DrawFocusRect(hdc, &focus);
        }
        return true;
    }

    void SetThumbnail(HWND thumbnail, const fs::path& file, bool hasValue)
    {
        SendMessageW(thumbnail, WM_SETTHUMBNAIL, hasValue ? 1 : 0, reinterpret_cast<LPARAM>(&file));
    }

    void ListViewSetRows(HWND list, const std::vector<std::vector<std::wstring>>& rows, int selected)
    {
        SendMessageW(list, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(list);
        for (size_t r = 0; r < rows.size(); ++r)
        {
            for (size_t c = 0; c < rows[r].size(); ++c)
            {
                std::wstring text = rows[r][c];
                // Show multi-line text on one line.
                std::replace(text.begin(), text.end(), L'\n', L' ');
                std::erase(text, L'\r');
                LVITEMW item{};
                item.mask = LVIF_TEXT;
                item.iItem = static_cast<int>(r);
                item.iSubItem = static_cast<int>(c);
                item.pszText = text.data();
                if (c == 0)
                    ListView_InsertItem(list, &item);
                else
                    ListView_SetItem(list, &item);
            }
        }
        if (selected >= 0 && selected < static_cast<int>(rows.size()))
        {
            ListView_SetItemState(list, selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(list, selected, FALSE);
        }
        SendMessageW(list, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(list, nullptr, TRUE);
    }

    int ListViewSelection(HWND list)
    {
        return ListView_GetNextItem(list, -1, LVNI_SELECTED);
    }

    void ListViewSelect(HWND list, int index)
    {
        ListView_SetItemState(list, -1, 0, LVIS_SELECTED);
        if (index >= 0)
        {
            ListView_SetItemState(list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(list, index, FALSE);
        }
    }

    // ------------------------------------------------------------------ pop-ups

    std::optional<std::wstring> Prompt(HWND owner, std::wstring_view title, std::wstring_view message,
                                       std::wstring_view initial, const Validator& validate)
    {
        PromptState state;
        state.message = message;
        state.initial = initial;
        state.validate = &validate;
        const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
        RECT client{ 0, 0, Scale(440), Scale(170) };
        AdjustWindowRectExForDpi(&client, style, FALSE, WS_EX_DLGMODALFRAME, g_dpi);
        const RECT rc = CenteredOn(owner, client.right - client.left, client.bottom - client.top);
        const std::wstring t(title);
        HWND popup = CreateWindowExW(WS_EX_DLGMODALFRAME, PromptClass, t.c_str(), style, rc.left, rc.top,
                                     rc.right - rc.left, rc.bottom - rc.top, owner, nullptr, g_instance, &state);
        if (!popup)
            return std::nullopt;
        RunModal(owner, popup, state.done, state.edit);
        return state.result;
    }

    void ShowImageFullScreen(HWND owner, const fs::path& file)
    {
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi);
        const RECT& r = mi.rcMonitor;
        ViewerState state;
        state.file = file;
        state.bitmap = LoadImageFit(file, r.right - r.left, r.bottom - r.top, true, &state.size);
        HWND popup = CreateWindowExW(WS_EX_TOPMOST, ViewerClass, L"Image", WS_POPUP, r.left, r.top, r.right - r.left,
                                     r.bottom - r.top, owner, nullptr, g_instance, &state);
        if (popup)
            RunModal(owner, popup, state.done);
        if (state.bitmap)
            DeleteObject(state.bitmap);
    }

    std::optional<std::string> PickImage(HWND owner, const fs::path& assets, std::string_view current)
    {
        PickerState state;
        state.assets = assets;
        const fs::path folder = assets / L"images";
        std::error_code ec;
        if (fs::is_directory(folder, ec))
        {
            for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator();
                 it.increment(ec))
            {
                if (!it->is_regular_file(ec))
                    continue;
                std::wstring ext = it->path().extension().wstring();
                for (auto& c : ext)
                    c = static_cast<wchar_t>(towlower(c));
                if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp" || ext == L".gif")
                    state.files.push_back(it->path());
            }
        }
        std::ranges::sort(state.files);
        const fs::path currentPath = assets / fs::path(Widen(current));
        for (size_t i = 0; i < state.files.size(); ++i)
        {
            if (!current.empty() && fs::equivalent(state.files[i], currentPath, ec))
                state.initial = static_cast<int>(i);
        }

        const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX;
        RECT client{ 0, 0, Scale(940), Scale(640) };
        AdjustWindowRectExForDpi(&client, style, FALSE, 0, g_dpi);
        const RECT rc = CenteredOn(owner, client.right - client.left, client.bottom - client.top);
        HWND popup = CreateWindowExW(0, PickerClass, L"Choose an image", style, rc.left, rc.top, rc.right - rc.left,
                                     rc.bottom - rc.top, owner, nullptr, g_instance, &state);
        if (!popup)
            return std::nullopt;
        RunModal(owner, popup, state.done, state.list);
        return state.result;
    }

    // ------------------------------------------------------------------ images

    HBITMAP LoadImageFit(const fs::path& file, int maxWidth, int maxHeight, bool allowUpscale, SIZE* size)
    {
        static ComPtr<IWICImagingFactory> factory;
        if (!factory &&
            FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
            return nullptr;
        if (maxWidth <= 0 || maxHeight <= 0 || file.empty())
            return nullptr;

        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(factory->CreateDecoderFromFilename(file.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)))
            return nullptr;
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0, &frame)))
            return nullptr;
        UINT w = 0, h = 0;
        frame->GetSize(&w, &h);
        if (w == 0 || h == 0)
            return nullptr;

        double scale = std::min(static_cast<double>(maxWidth) / w, static_cast<double>(maxHeight) / h);
        if (!allowUpscale)
            scale = std::min(scale, 1.0);
        const UINT tw = std::max(1u, static_cast<UINT>(w * scale));
        const UINT th = std::max(1u, static_cast<UINT>(h * scale));

        ComPtr<IWICBitmapScaler> scaler;
        if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
            FAILED(scaler->Initialize(frame.Get(), tw, th, WICBitmapInterpolationModeFant)))
            return nullptr;
        ComPtr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(&converter)) ||
            FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                         WICBitmapPaletteTypeCustom)))
            return nullptr;

        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = static_cast<LONG>(tw);
        bi.bmiHeader.biHeight = -static_cast<LONG>(th);
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap)
            return nullptr;
        if (FAILED(converter->CopyPixels(nullptr, tw * 4, tw * th * 4, static_cast<BYTE*>(bits))))
        {
            DeleteObject(bitmap);
            return nullptr;
        }
        if (size)
            *size = { static_cast<LONG>(tw), static_cast<LONG>(th) };
        return bitmap;
    }

    void DrawBitmap(HDC hdc, HBITMAP bitmap, SIZE size, const RECT& area)
    {
        HDC mem = CreateCompatibleDC(hdc);
        HGDIOBJ old = SelectObject(mem, bitmap);
        const int x = area.left + (area.right - area.left - size.cx) / 2;
        const int y = area.top + (area.bottom - area.top - size.cy) / 2;
        BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        AlphaBlend(hdc, x, y, size.cx, size.cy, mem, 0, 0, size.cx, size.cy, blend);
        SelectObject(mem, old);
        DeleteDC(mem);
    }
}
