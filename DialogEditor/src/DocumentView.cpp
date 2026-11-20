#include "DocumentView.h"
#include "DialogModel.h"
#include "MainWindow.h"
#include "Ui.h"
#include "Util.h"

namespace de
{
    namespace
    {
        constexpr wchar_t ViewportClass[] = L"DE_DocumentViewport";
        constexpr wchar_t CanvasClass[] = L"DE_DocumentCanvas";

        enum : int
        {
            IdLang = 2001, IdLangAdd, IdTitle,
            IdSegList, IdSegAdd, IdSegRemove, IdSegUp, IdSegDown,
            IdSegId, IdSegTerminal, IdBgThumb, IdBgChange, IdBgClear,
            IdEntList, IdEntAdd, IdEntRemove, IdEntUp, IdEntDown,
            IdSpeaker, IdText, IdPortraitThumb, IdPortraitChange, IdPortraitClear, IdKey,
            IdRespList, IdRespAdd, IdRespRemove, IdRespUp, IdRespDown, IdRespId, IdRespText, IdRespGoto,
            IdNextList, IdNextAdd, IdNextRemove, IdNextUp, IdNextDown, IdNextKey, IdNextLevel,
        };

        // RAII guard that marks the UI as "being filled from the model" so change
        // notifications don't write back.
        struct Updating
        {
            explicit Updating(int& counter) : m_counter(counter) { ++m_counter; }
            ~Updating() { --m_counter; }
            Updating(const Updating&) = delete;
            Updating& operator=(const Updating&) = delete;
            int& m_counter;
        };

        std::wstring W(std::string_view s) { return Widen(s); }

        HWND Header(HWND parent, std::wstring_view text) { return ui::Label(parent, text, ui::BoldFont()); }
        HWND Hint(HWND parent) { return ui::Label(parent, L"", ui::SmallFont()); }
    }

    DocumentView::~DocumentView()
    {
        if (m_viewport)
            DestroyWindow(m_viewport);
    }

    // ------------------------------------------------------------------ loading & saving

    bool DocumentView::Load(const fs::path& file, std::wstring& error)
    {
        auto text = ReadFileText(file);
        if (!text)
        {
            error = L"Could not read " + file.wstring();
            return false;
        }
        std::string parseError;
        if (!m_doc.Parse(*text, parseError))
        {
            error = file.filename().wstring() + L" is not valid JSON (" + W(parseError) + L").";
            return false;
        }
        if (!m_doc.Root() || !m_doc.Root()->IsObject())
        {
            error = file.filename().wstring() + L" does not contain a JSON object.";
            return false;
        }
        m_path = file;
        const auto languages = model::Languages(m_doc);
        m_language = languages.empty() || languages.contains("en") ? "en" : *languages.begin();
        return true;
    }

    bool DocumentView::Save(std::wstring& error)
    {
        if (!WriteFileText(m_path, m_doc.Serialize(), error))
            return false;
        m_modified = false;
        return true;
    }

    std::wstring DocumentView::Title() const
    {
        return W(model::DisplayLoc(m_doc.Root(), "title", "en"));
    }

    void DocumentView::SetModified()
    {
        if (m_updating > 0)
            return;
        m_modified = true;
        m_main.DocumentChanged(*this);
    }

    HWND DocumentView::Owner() const
    {
        return m_main.Hwnd();
    }

    // ------------------------------------------------------------------ window creation

    void DocumentView::Create(HWND parent)
    {
        static bool registered = false;
        if (!registered)
        {
            registered = true;
            WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
            wc.hInstance = ui::Instance();
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
            wc.lpfnWndProc = ViewportProc;
            wc.lpszClassName = ViewportClass;
            RegisterClassExW(&wc);
            wc.lpfnWndProc = CanvasProc;
            wc.lpszClassName = CanvasClass;
            RegisterClassExW(&wc);
        }

        m_viewport = CreateWindowExW(WS_EX_CONTROLPARENT, ViewportClass, L"", WS_CHILD | WS_VSCROLL | WS_CLIPCHILDREN, 0, 0,
                                     10, 10, parent, nullptr, ui::Instance(), this);
        m_canvas = CreateWindowExW(WS_EX_CONTROLPARENT, CanvasClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 10,
                                   10, m_viewport, nullptr, ui::Instance(), this);
        HWND c = m_canvas;

        // Language and title.
        m_langLabel = ui::Label(c, L"Language:");
        m_langCombo = ui::ComboList(c, IdLang);
        m_langAdd = ui::IconButton(c, ui::Glyph::Add, L"", IdLangAdd);
        m_titleLabel = ui::Label(c, L"Title:");
        m_titleEdit = ui::Edit(c, IdTitle);

        // Segments.
        m_segHeader = Header(c, L"Segments");
        m_segList = ui::ListBox(c, IdSegList);
        m_segAdd = ui::IconButton(c, ui::Glyph::Add, L"", IdSegAdd);
        m_segRemove = ui::IconButton(c, ui::Glyph::Remove, L"", IdSegRemove);
        m_segUp = ui::IconButton(c, ui::Glyph::Up, L"", IdSegUp);
        m_segDown = ui::IconButton(c, ui::Glyph::Down, L"", IdSegDown);
        m_idLabel = ui::Label(c, L"ID:");
        m_idEdit = ui::Edit(c, IdSegId);
        m_terminal = ui::CheckBox(c, L"Terminal (dialog ends after this segment)", IdSegTerminal);
        m_bgLabel = ui::Label(c, L"Background:");
        m_bgThumb = ui::Thumbnail(c, IdBgThumb);
        m_bgChange = ui::IconButton(c, ui::Glyph::Image, L"Change...", IdBgChange);
        m_bgClear = ui::Button(c, L"Clear", IdBgClear);
        m_bgPath = Hint(c);

        // Entries.
        m_entHeader = Header(c, L"Entries");
        m_entList = ui::ListView(c, IdEntList, { { L"#", 36 }, { L"Speaker", 150 }, { L"Text", 400 }, { L"Question key", 120 } });
        m_entAdd = ui::IconButton(c, ui::Glyph::Add, L"Add entry", IdEntAdd);
        m_entRemove = ui::IconButton(c, ui::Glyph::Remove, L"Remove", IdEntRemove);
        m_entUp = ui::IconButton(c, ui::Glyph::Up, L"Up", IdEntUp);
        m_entDown = ui::IconButton(c, ui::Glyph::Down, L"Down", IdEntDown);

        m_entryHeader = Header(c, L"Selected entry");
        m_speakerLabel = ui::Label(c, L"Speaker:");
        m_speakerEdit = ui::Edit(c, IdSpeaker);
        m_textLabel = ui::Label(c, L"Text:");
        m_textEdit = ui::Edit(c, IdText, true);
        m_portraitLabel = ui::Label(c, L"Portrait:");
        m_portraitThumb = ui::Thumbnail(c, IdPortraitThumb);
        m_portraitChange = ui::IconButton(c, ui::Glyph::Image, L"Change...", IdPortraitChange);
        m_portraitClear = ui::Button(c, L"Clear", IdPortraitClear);
        m_portraitPath = Hint(c);
        m_keyLabel = ui::Label(c, L"Question key:");
        m_keyEdit = ui::Edit(c, IdKey);
        m_keyHint = Hint(c);

        m_respLabel = ui::Label(c, L"Responses:");
        m_respList = ui::ListView(c, IdRespList, { { L"ID", 120 }, { L"Text", 360 }, { L"Goto", 140 } });
        m_respAdd = ui::IconButton(c, ui::Glyph::Add, L"Add response", IdRespAdd);
        m_respRemove = ui::IconButton(c, ui::Glyph::Remove, L"Remove", IdRespRemove);
        m_respUp = ui::IconButton(c, ui::Glyph::Up, L"Up", IdRespUp);
        m_respDown = ui::IconButton(c, ui::Glyph::Down, L"Down", IdRespDown);
        m_respIdLabel = ui::Label(c, L"ID:");
        m_respIdEdit = ui::Edit(c, IdRespId);
        m_respTextLabel = ui::Label(c, L"Text:");
        m_respTextEdit = ui::Edit(c, IdRespText);
        m_respGotoLabel = ui::Label(c, L"Goto:");
        m_respGotoCombo = ui::ComboList(c, IdRespGoto);

        // Next levels.
        m_nextHeader = Header(c, L"Next levels");
        m_nextList = ui::ListView(c, IdNextList, { { L"Key", 280 }, { L"Level", 360 } });
        m_nextAdd = ui::IconButton(c, ui::Glyph::Add, L"Add", IdNextAdd);
        m_nextRemove = ui::IconButton(c, ui::Glyph::Remove, L"Remove", IdNextRemove);
        m_nextUp = ui::IconButton(c, ui::Glyph::Up, L"Up", IdNextUp);
        m_nextDown = ui::IconButton(c, ui::Glyph::Down, L"Down", IdNextDown);
        m_nextKeyLabel = ui::Label(c, L"Key:");
        m_nextKeyEdit = ui::Edit(c, IdNextKey);
        m_nextLevelLabel = ui::Label(c, L"Level:");
        m_nextLevelCombo = ui::ComboEdit(c, IdNextLevel);
        m_nextHint = Hint(c);
        ui::SetText(m_nextHint, L"The first entry whose key matches the player's answers is used. A key is a list of "
                                L"conditions joined with &, e.g. \"route=belt & pursue=yes\" (also \"question!=answer\"). "
                                L"Use \"default\" (or *) for the fallback.");

        // Tooltips for the icon-only buttons.
        HWND tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT,
                                   CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, m_viewport, nullptr, ui::Instance(), nullptr);
        auto addTip = [&](HWND control, const wchar_t* text)
        {
            TOOLINFOW ti{};
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            ti.hwnd = c;
            ti.uId = reinterpret_cast<UINT_PTR>(control);
            ti.lpszText = const_cast<wchar_t*>(text);
            SendMessageW(tip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
        };
        addTip(m_langAdd, L"Add a language");
        addTip(m_segAdd, L"Add a segment after the selected one");
        addTip(m_segRemove, L"Remove the selected segment");
        addTip(m_segUp, L"Move the selected segment up");
        addTip(m_segDown, L"Move the selected segment down");
        addTip(m_bgThumb, L"Click to view full screen");
        addTip(m_portraitThumb, L"Click to view full screen");

        SendMessageW(m_speakerEdit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"(narration)"));
        SendMessageW(m_keyEdit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"(not a question)"));
        SendMessageW(m_titleEdit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"(optional, shown in the file list)"));

        RefreshLevelChoices();
        RefreshAll();
    }

    // ------------------------------------------------------------------ window procedures

    LRESULT CALLBACK DocumentView::ViewportProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == WM_NCCREATE)
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        auto* self = reinterpret_cast<DocumentView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self)
            return DefWindowProcW(hwnd, msg, wp, lp);
        switch (msg)
        {
        case WM_SIZE:
            if (self->m_canvas)
                self->Layout();
            return 0;
        case WM_VSCROLL:
        {
            SCROLLINFO si{ sizeof(si), SIF_ALL };
            GetScrollInfo(hwnd, SB_VERT, &si);
            int pos = si.nPos;
            switch (LOWORD(wp))
            {
            case SB_LINEUP: pos -= ui::Scale(40); break;
            case SB_LINEDOWN: pos += ui::Scale(40); break;
            case SB_PAGEUP: pos -= static_cast<int>(si.nPage); break;
            case SB_PAGEDOWN: pos += static_cast<int>(si.nPage); break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: pos = si.nTrackPos; break;
            case SB_TOP: pos = 0; break;
            case SB_BOTTOM: pos = si.nMax; break;
            default: break;
            }
            self->ScrollTo(pos);
            return 0;
        }
        case WM_MOUSEWHEEL:
            self->ScrollTo(self->m_scroll - GET_WHEEL_DELTA_WPARAM(wp) * ui::Scale(60) / WHEEL_DELTA);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    LRESULT CALLBACK DocumentView::CanvasProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == WM_NCCREATE)
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        auto* self = reinterpret_cast<DocumentView*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        return self ? self->HandleCanvas(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
    }

    LRESULT DocumentView::HandleCanvas(UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_COMMAND:
            OnCommand(LOWORD(wp), HIWORD(wp), reinterpret_cast<HWND>(lp));
            return 0;
        case WM_NOTIFY:
            OnNotify(*reinterpret_cast<const NMHDR*>(lp));
            return 0;
        case WM_DRAWITEM:
            ui::DrawIconButton(*reinterpret_cast<const DRAWITEMSTRUCT*>(lp));
            return TRUE;
        case WM_CTLCOLORSTATIC:
        {
            HDC hdc = reinterpret_cast<HDC>(wp);
            HWND control = reinterpret_cast<HWND>(lp);
            SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
            if (control == m_bgPath || control == m_portraitPath || control == m_nextHint)
                SetTextColor(hdc, GetSysColor(COLOR_GRAYTEXT));
            else if (control == m_keyHint)
                SetTextColor(hdc, RGB(170, 95, 0));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_MOUSEWHEEL:
            return SendMessageW(m_viewport, msg, wp, lp);
        default:
            break;
        }
        return DefWindowProcW(m_canvas, msg, wp, lp);
    }

    // ------------------------------------------------------------------ layout & scrolling

    void DocumentView::SetBounds(const RECT& bounds)
    {
        MoveWindow(m_viewport, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, TRUE);
    }

    void DocumentView::Show(bool visible)
    {
        ShowWindow(m_viewport, visible ? SW_SHOW : SW_HIDE);
        if (visible)
            SetWindowPos(m_viewport, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }

    void DocumentView::OnDpiChanged()
    {
        Layout();
    }

    void DocumentView::Layout()
    {
        RECT rc;
        GetClientRect(m_viewport, &rc);
        const int width = std::max<int>(rc.right, ui::Scale(720));
        auto S = ui::Scale;
        const int m = S(14), inner = width - 2 * m, rowH = S(26), btnH = S(28), labelW = S(100), gap = S(8);
        auto place = [](HWND h, int x, int y, int w, int hh) { MoveWindow(h, x, y, std::max(0, w), std::max(0, hh), FALSE); };
        auto buttonRow = [&](std::initializer_list<HWND> buttons, int x, int y, int w)
        {
            for (HWND b : buttons)
            {
                place(b, x, y, w, btnH);
                x += w + S(6);
            }
        };

        int y = m;
        // Language row.
        place(m_langLabel, m, y + S(4), S(76), S(20));
        place(m_langCombo, m + S(78), y, S(150), S(300));
        place(m_langAdd, m + S(234), y - 1, S(32), btnH);
        place(m_titleLabel, m + S(290), y + S(4), S(44), S(20));
        place(m_titleEdit, m + S(336), y, inner - S(336), rowH);
        y += rowH + S(18);

        // Segments.
        place(m_segHeader, m, y, inner, S(24));
        y += S(30);
        const int segW = S(240);
        place(m_segList, m, y, segW, S(170));
        const int segBtnW = (segW - 3 * S(6)) / 4;
        buttonRow({ m_segAdd, m_segRemove, m_segUp, m_segDown }, m, y + S(176), segBtnW);
        const int segBottom = y + S(176) + btnH;

        const int dx = m + segW + S(24);
        const int dw = inner - segW - S(24);
        place(m_idLabel, dx, y + S(4), labelW, S(20));
        place(m_idEdit, dx + labelW, y, S(220), rowH);
        place(m_terminal, dx + labelW + S(236), y + S(2), std::max(S(120), dw - labelW - S(236)), S(22));
        const int bgY = y + rowH + S(12);
        const int thumbW = S(240), thumbH = S(135);
        place(m_bgLabel, dx, bgY + S(4), labelW, S(20));
        place(m_bgThumb, dx + labelW, bgY, thumbW, thumbH);
        const int bx = dx + labelW + thumbW + S(12);
        place(m_bgChange, bx, bgY, S(120), btnH);
        place(m_bgClear, bx, bgY + btnH + S(6), S(120), btnH);
        place(m_bgPath, bx, bgY + 2 * (btnH + S(6)), std::max(S(80), dx + dw - bx), S(40));
        y = std::max(segBottom, bgY + thumbH) + S(22);

        // Entries.
        place(m_entHeader, m, y, inner, S(24));
        y += S(30);
        place(m_entList, m, y, inner, S(160));
        ListView_SetColumnWidth(m_entList, 2, std::max(S(120), inner - S(36 + 150 + 120 + 30)));
        y += S(166);
        buttonRow({ m_entAdd, m_entRemove, m_entUp, m_entDown }, m, y, S(116));
        y += btnH + S(16);

        // Selected entry.
        place(m_entryHeader, m, y, inner, S(24));
        y += S(30);
        const int portraitW = S(270);
        const int leftW = inner - portraitW - S(16);
        const int px = m + leftW + S(16);
        const int entryTop = y;
        place(m_speakerLabel, m, y + S(4), labelW, S(20));
        place(m_speakerEdit, m + labelW, y, leftW - labelW, rowH);
        y += rowH + gap;
        place(m_textLabel, m, y + S(4), labelW, S(20));
        place(m_textEdit, m + labelW, y, leftW - labelW, S(110));
        y += S(110) + gap;
        place(m_keyLabel, m, y + S(4), labelW, S(20));
        place(m_keyEdit, m + labelW, y, S(200), rowH);
        place(m_keyHint, m + labelW + S(210), y + S(4), leftW - labelW - S(210), S(36));
        y += rowH + S(14);

        place(m_portraitLabel, px, entryTop + S(4), S(80), S(20));
        place(m_portraitThumb, px, entryTop + S(26), S(120), S(120));
        place(m_portraitChange, px + S(128), entryTop + S(26), portraitW - S(128), btnH);
        place(m_portraitClear, px + S(128), entryTop + S(26) + btnH + S(6), portraitW - S(128), btnH);
        place(m_portraitPath, px, entryTop + S(150), portraitW, S(36));
        y = std::max(y, entryTop + S(190));

        // Responses.
        place(m_respLabel, m, y + S(4), labelW, S(20));
        const int respW = inner - labelW;
        place(m_respList, m + labelW, y, respW, S(116));
        ListView_SetColumnWidth(m_respList, 1, std::max(S(120), respW - S(120 + 140 + 30)));
        y += S(122);
        buttonRow({ m_respAdd, m_respRemove, m_respUp, m_respDown }, m + labelW, y, S(130));
        y += btnH + S(10);
        {
            int x = m + labelW;
            place(m_respIdLabel, x, y + S(4), S(28), S(20));
            place(m_respIdEdit, x + S(30), y, S(150), rowH);
            x += S(194);
            place(m_respGotoLabel, m + labelW + respW - S(220), y + S(4), S(44), S(20));
            place(m_respGotoCombo, m + labelW + respW - S(174), y, S(174), S(300));
            place(m_respTextLabel, x, y + S(4), S(40), S(20));
            place(m_respTextEdit, x + S(42), y, (m + labelW + respW - S(232)) - (x + S(42)), rowH);
        }
        y += rowH + S(24);

        // Next levels.
        place(m_nextHeader, m, y, inner, S(24));
        y += S(30);
        place(m_nextList, m, y, inner, S(130));
        ListView_SetColumnWidth(m_nextList, 1, std::max(S(120), inner - S(280 + 30)));
        y += S(136);
        buttonRow({ m_nextAdd, m_nextRemove, m_nextUp, m_nextDown }, m, y, S(100));
        y += btnH + S(10);
        place(m_nextKeyLabel, m, y + S(4), S(36), S(20));
        place(m_nextKeyEdit, m + S(40), y, S(300), rowH);
        place(m_nextLevelLabel, m + S(356), y + S(4), S(46), S(20));
        place(m_nextLevelCombo, m + S(404), y, inner - S(404), S(300));
        y += rowH + S(6);
        place(m_nextHint, m, y, inner, S(40));
        y += S(40) + m;

        m_contentHeight = y;
        const int viewHeight = rc.bottom;
        SCROLLINFO si{ sizeof(si), SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL };
        si.nMin = 0;
        si.nMax = m_contentHeight - 1;
        si.nPage = static_cast<UINT>(std::max(0, viewHeight));
        SetScrollInfo(m_viewport, SB_VERT, &si, TRUE);
        m_scroll = std::clamp(m_scroll, 0, std::max(0, m_contentHeight - viewHeight));
        SetWindowPos(m_canvas, nullptr, 0, -m_scroll, rc.right, std::max(m_contentHeight, viewHeight),
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SetScrollPos(m_viewport, SB_VERT, m_scroll, TRUE);
        InvalidateRect(m_canvas, nullptr, TRUE);
    }

    void DocumentView::ScrollTo(int position)
    {
        RECT rc;
        GetClientRect(m_viewport, &rc);
        position = std::clamp(position, 0, std::max(0, m_contentHeight - static_cast<int>(rc.bottom)));
        if (position == m_scroll)
            return;
        m_scroll = position;
        SetScrollPos(m_viewport, SB_VERT, m_scroll, TRUE);
        SetWindowPos(m_canvas, nullptr, 0, -m_scroll, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    void DocumentView::EnsureFocusVisible()
    {
        HWND focus = GetFocus();
        if (!focus || !IsChild(m_canvas, focus))
            return;
        // Combo boxes put focus on an inner edit; use the direct child of the canvas.
        while (GetParent(focus) != m_canvas)
            focus = GetParent(focus);
        RECT control, view;
        GetWindowRect(focus, &control);
        GetWindowRect(m_viewport, &view);
        const int margin = ui::Scale(16);
        if (control.top < view.top)
            ScrollTo(m_scroll - (view.top - control.top) - margin);
        else if (control.bottom > view.bottom)
            ScrollTo(m_scroll + (control.bottom - view.bottom) + margin);
    }

    // ------------------------------------------------------------------ model access

    DocumentView::Node* DocumentView::Segment()
    {
        Node* segments = model::Segments(m_doc, false);
        if (!segments || m_segment < 0)
            return nullptr;
        Node* s = segments->At(static_cast<size_t>(m_segment));
        return s && s->IsObject() ? s : nullptr;
    }

    DocumentView::Node* DocumentView::Entry()
    {
        Node* segment = Segment();
        if (!segment || m_entry < 0)
            return nullptr;
        Node* entries = model::Entries(m_doc, *segment, false);
        Node* e = entries ? entries->At(static_cast<size_t>(m_entry)) : nullptr;
        return e && e->IsObject() ? e : nullptr;
    }

    DocumentView::Node* DocumentView::Response()
    {
        Node* entry = Entry();
        if (!entry || m_response < 0)
            return nullptr;
        Node* responses = model::Responses(m_doc, *entry, false);
        Node* r = responses ? responses->At(static_cast<size_t>(m_response)) : nullptr;
        return r && r->IsObject() ? r : nullptr;
    }

    DocumentView::Node* DocumentView::NextLevel()
    {
        Node* next = model::NextLevels(m_doc, false);
        if (!next || m_next < 0)
            return nullptr;
        Node* n = next->At(static_cast<size_t>(m_next));
        return n && n->IsObject() ? n : nullptr;
    }

    // ------------------------------------------------------------------ refresh

    void DocumentView::RefreshAll()
    {
        RefreshLanguages(m_language);
        RefreshTitle();
        if (m_segment < 0)
            m_segment = 0;
        RefreshSegments();
        if (m_next < 0)
            m_next = 0;
        RefreshNextLevels();
    }

    void DocumentView::RefreshLanguages(const std::string& select)
    {
        Updating guard(m_updating);
        std::set<std::string> languages = model::Languages(m_doc);
        languages.insert(m_addedLanguages.begin(), m_addedLanguages.end());
        if (languages.empty())
            languages.insert("en");
        m_language = languages.contains(select) ? select : (languages.contains("en") ? "en" : *languages.begin());
        SendMessageW(m_langCombo, CB_RESETCONTENT, 0, 0);
        int index = 0, selected = 0;
        for (const std::string& lang : languages)
        {
            SendMessageW(m_langCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(W(lang).c_str()));
            if (lang == m_language)
                selected = index;
            ++index;
        }
        SendMessageW(m_langCombo, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
    }

    void DocumentView::RefreshTitle()
    {
        Updating guard(m_updating);
        ui::SetText(m_titleEdit, W(model::GetLoc(m_doc.Root(), "title", m_language)));
    }

    void DocumentView::RefreshSegments()
    {
        {
            Updating guard(m_updating);
            SendMessageW(m_segList, LB_RESETCONTENT, 0, 0);
            Node* segments = model::Segments(m_doc, false);
            const int count = segments ? static_cast<int>(segments->Size()) : 0;
            for (int i = 0; i < count; ++i)
            {
                std::wstring id = W(model::GetString(segments->At(static_cast<size_t>(i)), "id"));
                if (id.empty())
                    id = L"(no id)";
                SendMessageW(m_segList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(id.c_str()));
            }
            m_segment = count == 0 ? -1 : std::clamp(m_segment, 0, count - 1);
            SendMessageW(m_segList, LB_SETCURSEL, static_cast<WPARAM>(m_segment), 0);
        }
        RefreshGotoCombo();
        RefreshSegmentDetails();
    }

    void DocumentView::RefreshSegmentDetails()
    {
        {
            Updating guard(m_updating);
            Node* segment = Segment();
            ui::SetText(m_idEdit, segment ? W(model::GetString(segment, "id")) : L"");
            const Node* terminal = segment ? segment->Find("terminal") : nullptr;
            Button_SetCheck(m_terminal, terminal && terminal->kind == json::Kind::Bool && terminal->boolean ? BST_CHECKED
                                                                                                           : BST_UNCHECKED);
            const std::string background = segment ? model::GetString(segment, "background") : std::string();
            ui::SetThumbnail(m_bgThumb, m_main.AssetsFolder() / fs::path(W(background)), !background.empty());
            ui::SetText(m_bgPath, background.empty() ? L"No background" : W(background));
        }
        m_entry = 0;
        RefreshEntries();
    }

    void DocumentView::RefreshEntries()
    {
        {
            Updating guard(m_updating);
            std::vector<std::vector<std::wstring>> rows;
            if (Node* segment = Segment())
            {
                if (Node* entries = model::Entries(m_doc, *segment, false))
                {
                    for (size_t i = 0; i < entries->Size(); ++i)
                    {
                        Node* e = entries->At(i);
                        rows.push_back({ std::to_wstring(i + 1), W(model::GetLoc(e, "speaker", m_language)),
                                         W(model::GetLoc(e ? &model::TextHolder(*e) : nullptr, "text", m_language)),
                                         W(e ? model::GetString(&model::QuestionHolder(*e), "key") : std::string()) });
                    }
                }
            }
            const int count = static_cast<int>(rows.size());
            m_entry = count == 0 ? -1 : std::clamp(m_entry, 0, count - 1);
            ui::ListViewSetRows(m_entList, rows, m_entry);
        }
        RefreshEntryDetails();
    }

    void DocumentView::UpdateEntryRow(int index)
    {
        Node* entry = Entry();
        if (!entry || index < 0)
            return;
        std::wstring speaker = W(model::GetLoc(entry, "speaker", m_language));
        std::wstring text = W(model::GetLoc(&model::TextHolder(*entry), "text", m_language));
        std::replace(text.begin(), text.end(), L'\n', L' ');
        std::wstring key = W(model::GetString(&model::QuestionHolder(*entry), "key"));
        ListView_SetItemText(m_entList, index, 1, speaker.data());
        ListView_SetItemText(m_entList, index, 2, text.data());
        ListView_SetItemText(m_entList, index, 3, key.data());
    }

    void DocumentView::RefreshEntryDetails()
    {
        {
            Updating guard(m_updating);
            Node* entry = Entry();
            ui::SetText(m_speakerEdit, entry ? W(model::GetLoc(entry, "speaker", m_language)) : L"");
            ui::SetText(m_textEdit,
                        entry ? ToEditText(model::GetLoc(&model::TextHolder(*entry), "text", m_language)) : L"");
            const std::string portrait = entry ? model::GetString(entry, "portrait") : std::string();
            ui::SetThumbnail(m_portraitThumb, m_main.AssetsFolder() / fs::path(W(portrait)), !portrait.empty());
            ui::SetText(m_portraitPath, portrait.empty() ? L"No portrait" : W(portrait));
            ui::SetText(m_keyEdit, entry ? W(model::GetString(&model::QuestionHolder(*entry), "key")) : L"");
        }
        m_response = 0;
        RefreshResponses();
    }

    void DocumentView::RefreshResponses()
    {
        {
            Updating guard(m_updating);
            std::vector<std::vector<std::wstring>> rows;
            Node* entry = Entry();
            if (Node* responses = entry ? model::Responses(m_doc, *entry, false) : nullptr)
            {
                for (size_t i = 0; i < responses->Size(); ++i)
                {
                    Node* r = responses->At(i);
                    rows.push_back({ W(model::ResponseId(r)), W(model::GetLoc(r, "text", m_language)),
                                     W(model::GetString(r, "goto")) });
                }
            }
            const int count = static_cast<int>(rows.size());
            m_response = count == 0 ? -1 : std::clamp(m_response, 0, count - 1);
            ui::ListViewSetRows(m_respList, rows, m_response);

            // Explain how questions work for the current entry.
            const std::string key = entry ? model::GetString(&model::QuestionHolder(*entry), "key") : std::string();
            if (count > 0 && key.empty())
                ui::SetText(m_keyHint, L"Set a key so the player's answer is recorded for Next levels.");
            else if (count == 0 && !key.empty())
                ui::SetText(m_keyHint, L"Add responses for the player to choose from.");
            else
                ui::SetText(m_keyHint, L"");
        }
        RefreshResponseDetails();
    }

    void DocumentView::UpdateResponseRow(int index)
    {
        Node* r = Response();
        if (!r || index < 0)
            return;
        std::wstring id = W(model::ResponseId(r));
        std::wstring text = W(model::GetLoc(r, "text", m_language));
        std::wstring target = W(model::GetString(r, "goto"));
        ListView_SetItemText(m_respList, index, 0, id.data());
        ListView_SetItemText(m_respList, index, 1, text.data());
        ListView_SetItemText(m_respList, index, 2, target.data());
    }

    void DocumentView::RefreshGotoCombo()
    {
        Updating guard(m_updating);
        SendMessageW(m_respGotoCombo, CB_RESETCONTENT, 0, 0);
        SendMessageW(m_respGotoCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"(none - continue)"));
        if (Node* segments = model::Segments(m_doc, false))
        {
            for (size_t i = 0; i < segments->Size(); ++i)
            {
                const std::wstring id = W(model::GetString(segments->At(i), "id"));
                if (!id.empty())
                    SendMessageW(m_respGotoCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(id.c_str()));
            }
        }
        RefreshResponseDetails();
    }

    void DocumentView::RefreshResponseDetails()
    {
        {
            Updating guard(m_updating);
            Node* r = Response();
            ui::SetText(m_respIdEdit, r ? W(model::ResponseId(r)) : L"");
            ui::SetText(m_respTextEdit, r ? W(model::GetLoc(r, "text", m_language)) : L"");
            const std::wstring target = r ? W(model::GetString(r, "goto")) : L"";
            int index = 0;
            if (!target.empty())
            {
                index = static_cast<int>(SendMessageW(m_respGotoCombo, CB_FINDSTRINGEXACT, 0, reinterpret_cast<LPARAM>(target.c_str())));
                if (index < 0)
                {
                    // A goto to a segment that doesn't exist: show it so it can be fixed.
                    const std::wstring missing = target + L" (missing)";
                    index = static_cast<int>(SendMessageW(m_respGotoCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(missing.c_str())));
                }
            }
            SendMessageW(m_respGotoCombo, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
        }
        UpdateEnabled();
    }

    void DocumentView::RefreshNextLevels()
    {
        {
            Updating guard(m_updating);
            std::vector<std::vector<std::wstring>> rows;
            if (Node* next = model::NextLevels(m_doc, false))
            {
                for (size_t i = 0; i < next->Size(); ++i)
                {
                    Node* n = next->At(i);
                    rows.push_back({ W(model::GetString(n, "key")), W(model::GetString(n, "level")) });
                }
            }
            const int count = static_cast<int>(rows.size());
            m_next = count == 0 ? -1 : std::clamp(m_next, 0, count - 1);
            ui::ListViewSetRows(m_nextList, rows, m_next);
        }
        RefreshNextDetails();
    }

    void DocumentView::UpdateNextRow(int index)
    {
        Node* n = NextLevel();
        if (!n || index < 0)
            return;
        std::wstring key = W(model::GetString(n, "key"));
        std::wstring level = W(model::GetString(n, "level"));
        ListView_SetItemText(m_nextList, index, 0, key.data());
        ListView_SetItemText(m_nextList, index, 1, level.data());
    }

    void DocumentView::RefreshNextDetails()
    {
        {
            Updating guard(m_updating);
            Node* n = NextLevel();
            ui::SetText(m_nextKeyEdit, n ? W(model::GetString(n, "key")) : L"");
            ui::SetText(m_nextLevelCombo, n ? W(model::GetString(n, "level")) : L"");
        }
        UpdateEnabled();
    }

    void DocumentView::RefreshLevelChoices()
    {
        Updating guard(m_updating);
        SendMessageW(m_nextLevelCombo, CB_RESETCONTENT, 0, 0);
        std::vector<std::wstring> levels;
        std::error_code ec;
        const fs::path folder = m_main.AssetsFolder() / L"levels";
        if (fs::is_directory(folder, ec))
        {
            for (const auto& entry : fs::directory_iterator(folder, ec))
                if (entry.is_regular_file(ec) && EqualsNoCase(entry.path().extension().wstring(), L".json"))
                    levels.push_back(L"levels/" + entry.path().filename().wstring());
        }
        std::ranges::sort(levels);
        for (const auto& level : levels)
            SendMessageW(m_nextLevelCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(level.c_str()));
    }

    void DocumentView::UpdateEnabled()
    {
        Node* segments = model::Segments(m_doc, false);
        const int segCount = segments ? static_cast<int>(segments->Size()) : 0;
        const bool seg = Segment() != nullptr;
        const bool entry = Entry() != nullptr;
        const bool response = Response() != nullptr;
        const bool next = NextLevel() != nullptr;

        auto enable = [](std::initializer_list<HWND> controls, bool on)
        {
            for (HWND h : controls)
                EnableWindow(h, on ? TRUE : FALSE);
        };
        enable({ m_segRemove, m_idEdit, m_terminal, m_bgThumb, m_bgChange, m_bgClear, m_entList, m_entAdd }, seg);
        EnableWindow(m_segUp, seg && m_segment > 0);
        EnableWindow(m_segDown, seg && m_segment + 1 < segCount);

        Node* entries = seg ? model::Entries(m_doc, *Segment(), false) : nullptr;
        const int entryCount = entries ? static_cast<int>(entries->Size()) : 0;
        enable({ m_entRemove, m_speakerEdit, m_textEdit, m_portraitThumb, m_portraitChange, m_portraitClear, m_keyEdit,
                 m_respList, m_respAdd },
               entry);
        EnableWindow(m_entUp, entry && m_entry > 0);
        EnableWindow(m_entDown, entry && m_entry + 1 < entryCount);

        Node* responses = entry ? model::Responses(m_doc, *Entry(), false) : nullptr;
        const int responseCount = responses ? static_cast<int>(responses->Size()) : 0;
        enable({ m_respRemove, m_respIdEdit, m_respTextEdit, m_respGotoCombo }, response);
        EnableWindow(m_respUp, response && m_response > 0);
        EnableWindow(m_respDown, response && m_response + 1 < responseCount);

        Node* nextLevels = model::NextLevels(m_doc, false);
        const int nextCount = nextLevels ? static_cast<int>(nextLevels->Size()) : 0;
        enable({ m_nextRemove, m_nextKeyEdit, m_nextLevelCombo }, next);
        EnableWindow(m_nextUp, next && m_next > 0);
        EnableWindow(m_nextDown, next && m_next + 1 < nextCount);
    }

    // ------------------------------------------------------------------ notifications

    void DocumentView::OnNotify(const NMHDR& hdr)
    {
        if (hdr.code != LVN_ITEMCHANGED || m_updating > 0)
            return;
        const auto& change = reinterpret_cast<const NMLISTVIEW&>(hdr);
        if (!(change.uChanged & LVIF_STATE) || !(change.uNewState & LVIS_SELECTED) || (change.uOldState & LVIS_SELECTED))
            return;
        if (hdr.hwndFrom == m_entList)
        {
            m_entry = change.iItem;
            RefreshEntryDetails();
        }
        else if (hdr.hwndFrom == m_respList)
        {
            m_response = change.iItem;
            RefreshResponseDetails();
        }
        else if (hdr.hwndFrom == m_nextList)
        {
            m_next = change.iItem;
            RefreshNextDetails();
        }
    }

    void DocumentView::OnCommand(int id, int code, HWND control)
    {
        (void)control;
        const bool clicked = code == BN_CLICKED;
        const bool changed = code == EN_CHANGE && m_updating == 0;

        switch (id)
        {
        // ---- language & title
        case IdLang:
            if (code == CBN_SELCHANGE && m_updating == 0)
            {
                const auto index = SendMessageW(m_langCombo, CB_GETCURSEL, 0, 0);
                wchar_t buffer[64]{};
                SendMessageW(m_langCombo, CB_GETLBTEXT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(buffer));
                m_language = Narrow(buffer);
                RefreshTitle();
                // Re-show the same entry and response in the new language.
                const int response = m_response;
                RefreshEntries();
                m_response = response;
                RefreshResponses();
            }
            return;
        case IdLangAdd:
            if (clicked)
                AddLanguage();
            return;
        case IdTitle:
            if (changed)
            {
                model::SetLoc(m_doc, *m_doc.Root(), "title", m_language, Narrow(ui::GetText(m_titleEdit)));
                SetModified();
            }
            return;

        // ---- segments
        case IdSegList:
            if (code == LBN_SELCHANGE && m_updating == 0)
            {
                m_segment = static_cast<int>(SendMessageW(m_segList, LB_GETCURSEL, 0, 0));
                RefreshSegmentDetails();
            }
            return;
        case IdSegAdd: if (clicked) AddSegment(); return;
        case IdSegRemove: if (clicked) RemoveSegment(); return;
        case IdSegUp: if (clicked) MoveSegment(-1); return;
        case IdSegDown: if (clicked) MoveSegment(1); return;
        case IdSegId:
            if (code == EN_SETFOCUS)
            {
                m_idBeforeEdit = model::GetString(Segment(), "id");
            }
            else if (changed)
            {
                if (Node* segment = Segment())
                {
                    const std::wstring id = ui::GetText(m_idEdit);
                    model::SetString(m_doc, *segment, "id", Narrow(id), false);
                    Updating guard(m_updating);
                    SendMessageW(m_segList, LB_DELETESTRING, static_cast<WPARAM>(m_segment), 0);
                    SendMessageW(m_segList, LB_INSERTSTRING, static_cast<WPARAM>(m_segment),
                                 reinterpret_cast<LPARAM>(id.empty() ? L"(no id)" : id.c_str()));
                    SendMessageW(m_segList, LB_SETCURSEL, static_cast<WPARAM>(m_segment), 0);
                    SetModified();
                }
            }
            else if (code == EN_KILLFOCUS)
            {
                // Keep "goto" references pointing at the renamed segment.
                const std::string newId = model::GetString(Segment(), "id");
                if (!m_idBeforeEdit.empty() && newId != m_idBeforeEdit)
                {
                    if (model::RenameGotoTargets(m_doc, m_idBeforeEdit, newId) > 0)
                    {
                        SetModified();
                        RefreshResponses();
                    }
                    RefreshGotoCombo();
                }
                m_idBeforeEdit.clear();
            }
            return;
        case IdSegTerminal:
            if (clicked && m_updating == 0)
            {
                if (Node* segment = Segment())
                {
                    if (Button_GetCheck(m_terminal) == BST_CHECKED)
                        m_doc.Set(*segment, "terminal", json::MakeBool(true));
                    else
                        m_doc.Remove(*segment, "terminal");
                    SetModified();
                }
            }
            return;
        case IdBgThumb: if (code == STN_CLICKED) ShowImage(true); return;
        case IdBgChange: if (clicked) ChooseImage(true); return;
        case IdBgClear: if (clicked) ClearImage(true); return;

        // ---- entries
        case IdEntAdd: if (clicked) AddEntry(); return;
        case IdEntRemove: if (clicked) RemoveEntry(); return;
        case IdEntUp: if (clicked) MoveEntry(-1); return;
        case IdEntDown: if (clicked) MoveEntry(1); return;
        case IdSpeaker:
            if (changed)
                if (Node* entry = Entry())
                {
                    model::SetLoc(m_doc, *entry, "speaker", m_language, Narrow(ui::GetText(m_speakerEdit)));
                    UpdateEntryRow(m_entry);
                    SetModified();
                }
            return;
        case IdText:
            if (changed)
                if (Node* entry = Entry())
                {
                    model::SetLoc(m_doc, model::TextHolder(*entry), "text", m_language, FromEditText(ui::GetText(m_textEdit)));
                    UpdateEntryRow(m_entry);
                    SetModified();
                }
            return;
        case IdKey:
            if (changed)
                if (Node* entry = Entry())
                {
                    model::SetString(m_doc, model::QuestionHolder(*entry), "key", Narrow(ui::GetText(m_keyEdit)), true);
                    UpdateEntryRow(m_entry);
                    SetModified();
                    RefreshResponses(); // updates the question hint; keeps the selection
                }
            return;
        case IdPortraitThumb: if (code == STN_CLICKED) ShowImage(false); return;
        case IdPortraitChange: if (clicked) ChooseImage(false); return;
        case IdPortraitClear: if (clicked) ClearImage(false); return;

        // ---- responses
        case IdRespAdd: if (clicked) AddResponse(); return;
        case IdRespRemove: if (clicked) RemoveResponse(); return;
        case IdRespUp: if (clicked) MoveResponse(-1); return;
        case IdRespDown: if (clicked) MoveResponse(1); return;
        case IdRespId:
            if (changed)
                if (Node* r = Response())
                {
                    model::SetResponseId(m_doc, *r, Narrow(ui::GetText(m_respIdEdit)));
                    UpdateResponseRow(m_response);
                    SetModified();
                }
            return;
        case IdRespText:
            if (changed)
                if (Node* r = Response())
                {
                    model::SetLoc(m_doc, *r, "text", m_language, Narrow(ui::GetText(m_respTextEdit)));
                    UpdateResponseRow(m_response);
                    SetModified();
                }
            return;
        case IdRespGoto:
            if (code == CBN_SELCHANGE && m_updating == 0)
                if (Node* r = Response())
                {
                    const auto index = SendMessageW(m_respGotoCombo, CB_GETCURSEL, 0, 0);
                    if (index <= 0)
                    {
                        m_doc.Remove(*r, "goto");
                    }
                    else
                    {
                        const auto length = SendMessageW(m_respGotoCombo, CB_GETLBTEXTLEN, static_cast<WPARAM>(index), 0);
                        std::wstring text(static_cast<size_t>(length) + 1, L'\0');
                        SendMessageW(m_respGotoCombo, CB_GETLBTEXT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(text.data()));
                        text.resize(static_cast<size_t>(length));
                        if (text.ends_with(L" (missing)"))
                            text.resize(text.size() - 10);
                        m_doc.SetString(*r, "goto", Narrow(text));
                    }
                    UpdateResponseRow(m_response);
                    SetModified();
                }
            return;

        // ---- next levels
        case IdNextAdd: if (clicked) AddNextLevel(); return;
        case IdNextRemove: if (clicked) RemoveNextLevel(); return;
        case IdNextUp: if (clicked) MoveNextLevel(-1); return;
        case IdNextDown: if (clicked) MoveNextLevel(1); return;
        case IdNextKey:
            if (changed)
                if (Node* n = NextLevel())
                {
                    m_doc.SetString(*n, "key", Narrow(ui::GetText(m_nextKeyEdit)));
                    UpdateNextRow(m_next);
                    SetModified();
                }
            return;
        case IdNextLevel:
            if ((code == CBN_EDITCHANGE || code == CBN_SELCHANGE) && m_updating == 0)
                if (Node* n = NextLevel())
                {
                    std::wstring text;
                    if (code == CBN_SELCHANGE)
                    {
                        const auto index = SendMessageW(m_nextLevelCombo, CB_GETCURSEL, 0, 0);
                        if (index < 0)
                            return;
                        const auto length = SendMessageW(m_nextLevelCombo, CB_GETLBTEXTLEN, static_cast<WPARAM>(index), 0);
                        text.resize(static_cast<size_t>(length) + 1);
                        SendMessageW(m_nextLevelCombo, CB_GETLBTEXT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(text.data()));
                        text.resize(static_cast<size_t>(length));
                    }
                    else
                    {
                        text = ui::GetText(m_nextLevelCombo);
                    }
                    m_doc.SetString(*n, "level", Narrow(text));
                    UpdateNextRow(m_next);
                    SetModified();
                }
            return;
        default:
            return;
        }
    }

    // ------------------------------------------------------------------ commands

    void DocumentView::AddLanguage()
    {
        std::set<std::string> existing = model::Languages(m_doc);
        existing.insert(m_addedLanguages.begin(), m_addedLanguages.end());
        const auto result = ui::Prompt(Owner(), L"Add language",
                                       L"Language code for the new language (for example \"fr\" or \"pt-br\"):", L"",
                                       [&](std::wstring& value) -> std::wstring
                                       {
                                           std::string code = Narrow(value);
                                           if (!model::NormalizeLanguageCode(code))
                                               return L"That doesn't look like a language code. Use 2-3 letters, optionally "
                                                      L"followed by -region (e.g. pt-br).";
                                           if (existing.contains(code))
                                               return L"\"" + Widen(code) + L"\" is already in this dialog.";
                                           value = Widen(code);
                                           return {};
                                       });
        if (!result)
            return;
        const std::string code = Narrow(*result);
        m_addedLanguages.insert(code);
        RefreshLanguages(code);
        RefreshTitle();
        RefreshEntries();
    }

    void DocumentView::AddSegment()
    {
        Node* segments = model::Segments(m_doc, true);
        const size_t index = m_segment >= 0 ? static_cast<size_t>(m_segment) + 1 : segments->Size();
        const std::string id = model::UniqueSegmentId(m_doc);
        Node* segment = m_doc.Insert(*segments, index, json::MakeObject());
        m_doc.SetString(*segment, "id", id);
        m_doc.Set(*segment, "entries", json::MakeArray());
        m_segment = static_cast<int>(index);
        SetModified();
        RefreshSegments();
        SetFocus(m_idEdit);
        SendMessageW(m_idEdit, EM_SETSEL, 0, -1);
    }

    void DocumentView::RemoveSegment()
    {
        Node* segment = Segment();
        if (!segment)
            return;
        Node* entries = model::Entries(m_doc, *segment, false);
        const size_t count = entries ? entries->Size() : 0;
        const std::wstring id = W(model::GetString(segment, "id"));
        const std::wstring message = std::format(L"Remove segment \"{}\"{}?", id,
                                                 count ? std::format(L" and its {} entr{}", count, count == 1 ? L"y" : L"ies") : L"");
        if (MessageBoxW(Owner(), message.c_str(), L"Remove segment", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
        m_doc.RemoveAt(*model::Segments(m_doc, false), static_cast<size_t>(m_segment));
        SetModified();
        RefreshSegments();
    }

    void DocumentView::MoveSegment(int delta)
    {
        Node* segments = model::Segments(m_doc, false);
        if (!segments || m_segment < 0)
            return;
        const int target = m_segment + delta;
        if (target < 0 || target >= static_cast<int>(segments->Size()))
            return;
        m_doc.Move(*segments, static_cast<size_t>(m_segment), static_cast<size_t>(target));
        m_segment = target;
        SetModified();
        RefreshSegments();
    }

    void DocumentView::AddEntry()
    {
        Node* segment = Segment();
        if (!segment)
            return;
        Node* current = Entry();
        Node* entries = model::Entries(m_doc, *segment, true);
        const size_t index = m_entry >= 0 ? static_cast<size_t>(m_entry) + 1 : entries->Size();
        Node* entry = m_doc.Insert(*entries, index, json::MakeObject());

        // Continue with the same speaker and portrait, which is the common case.
        if (const Node* speaker = current ? current->Find("speaker") : nullptr)
            m_doc.Set(*entry, "speaker", json::Clone(*speaker));
        else
            m_doc.SetString(*m_doc.Set(*entry, "speaker", json::MakeObject()), m_language, "");
        if (const Node* portrait = current ? current->Find("portrait") : nullptr)
            m_doc.Set(*entry, "portrait", json::Clone(*portrait));
        m_doc.SetString(*m_doc.Set(*entry, "text", json::MakeObject()), m_language, "");

        m_entry = static_cast<int>(index);
        SetModified();
        RefreshEntries();
        SetFocus(m_textEdit);
    }

    void DocumentView::RemoveEntry()
    {
        Node* segment = Segment();
        if (!Entry() || !segment)
            return;
        m_doc.RemoveAt(*model::Entries(m_doc, *segment, false), static_cast<size_t>(m_entry));
        SetModified();
        RefreshEntries();
    }

    void DocumentView::MoveEntry(int delta)
    {
        Node* segment = Segment();
        Node* entries = segment ? model::Entries(m_doc, *segment, false) : nullptr;
        if (!entries || m_entry < 0)
            return;
        const int target = m_entry + delta;
        if (target < 0 || target >= static_cast<int>(entries->Size()))
            return;
        m_doc.Move(*entries, static_cast<size_t>(m_entry), static_cast<size_t>(target));
        m_entry = target;
        SetModified();
        RefreshEntries();
    }

    void DocumentView::AddResponse()
    {
        Node* entry = Entry();
        if (!entry)
            return;
        Node* responses = model::Responses(m_doc, *entry, true);
        std::set<std::string> used;
        for (size_t i = 0; i < responses->Size(); ++i)
            used.insert(model::ResponseId(responses->At(i)));
        std::string id;
        for (int n = static_cast<int>(responses->Size()) + 1;; ++n)
        {
            id = std::format("answer{}", n);
            if (!used.contains(id))
                break;
        }
        const size_t index = m_response >= 0 ? static_cast<size_t>(m_response) + 1 : responses->Size();
        Node* response = m_doc.Insert(*responses, index, json::MakeObject());
        m_doc.SetString(*response, "id", id);
        m_doc.SetString(*m_doc.Set(*response, "text", json::MakeObject()), m_language, "");
        m_response = static_cast<int>(index);
        SetModified();
        UpdateEntryRow(m_entry);
        RefreshResponses();
        SetFocus(m_respTextEdit);
    }

    void DocumentView::RemoveResponse()
    {
        Node* entry = Entry();
        if (!entry || !Response())
            return;
        Node* responses = model::Responses(m_doc, *entry, false);
        m_doc.RemoveAt(*responses, static_cast<size_t>(m_response));
        if (responses->Size() == 0)
            m_doc.Remove(model::QuestionHolder(*entry), "responses");
        SetModified();
        RefreshResponses();
    }

    void DocumentView::MoveResponse(int delta)
    {
        Node* entry = Entry();
        Node* responses = entry ? model::Responses(m_doc, *entry, false) : nullptr;
        if (!responses || m_response < 0)
            return;
        const int target = m_response + delta;
        if (target < 0 || target >= static_cast<int>(responses->Size()))
            return;
        m_doc.Move(*responses, static_cast<size_t>(m_response), static_cast<size_t>(target));
        m_response = target;
        SetModified();
        RefreshResponses();
    }

    void DocumentView::AddNextLevel()
    {
        Node* next = model::NextLevels(m_doc, true);
        const bool first = next->Size() == 0;
        const size_t index = m_next >= 0 ? static_cast<size_t>(m_next) + 1 : next->Size();
        Node* option = m_doc.Insert(*next, index, json::MakeObject());
        m_doc.SetString(*option, "key", first ? "default" : "");
        m_doc.SetString(*option, "level", "");
        m_next = static_cast<int>(index);
        SetModified();
        RefreshNextLevels();
        SetFocus(first ? m_nextLevelCombo : m_nextKeyEdit);
    }

    void DocumentView::RemoveNextLevel()
    {
        Node* next = model::NextLevels(m_doc, false);
        if (!next || !NextLevel())
            return;
        m_doc.RemoveAt(*next, static_cast<size_t>(m_next));
        SetModified();
        RefreshNextLevels();
    }

    void DocumentView::MoveNextLevel(int delta)
    {
        Node* next = model::NextLevels(m_doc, false);
        if (!next || m_next < 0)
            return;
        const int target = m_next + delta;
        if (target < 0 || target >= static_cast<int>(next->Size()))
            return;
        m_doc.Move(*next, static_cast<size_t>(m_next), static_cast<size_t>(target));
        m_next = target;
        SetModified();
        RefreshNextLevels();
    }

    void DocumentView::ChooseImage(bool background)
    {
        Node* target = background ? Segment() : Entry();
        if (!target)
            return;
        const char* key = background ? "background" : "portrait";
        const auto chosen = ui::PickImage(Owner(), m_main.AssetsFolder(), model::GetString(target, key));
        if (!chosen)
            return;
        m_doc.SetString(*target, key, *chosen);
        SetModified();
        Updating guard(m_updating);
        HWND thumb = background ? m_bgThumb : m_portraitThumb;
        ui::SetThumbnail(thumb, m_main.AssetsFolder() / fs::path(W(*chosen)), true);
        ui::SetText(background ? m_bgPath : m_portraitPath, W(*chosen));
    }

    void DocumentView::ClearImage(bool background)
    {
        Node* target = background ? Segment() : Entry();
        if (!target)
            return;
        m_doc.Remove(*target, background ? "background" : "portrait");
        SetModified();
        ui::SetThumbnail(background ? m_bgThumb : m_portraitThumb, {}, false);
        ui::SetText(background ? m_bgPath : m_portraitPath, background ? L"No background" : L"No portrait");
    }

    void DocumentView::ShowImage(bool background)
    {
        Node* target = background ? Segment() : Entry();
        const std::string file = model::GetString(target, background ? "background" : "portrait");
        if (file.empty())
            return;
        const fs::path path = m_main.AssetsFolder() / fs::path(W(file));
        std::error_code ec;
        if (fs::exists(path, ec))
            ui::ShowImageFullScreen(Owner(), path);
    }
}
