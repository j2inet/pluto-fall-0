#include "MainWindow.h"
#include "DialogModel.h"
#include "DocumentView.h"
#include "Ui.h"
#include "Util.h"

namespace de
{
    namespace
    {
        constexpr wchar_t ClassName[] = L"DE_MainWindow";
        constexpr int IdTabs = 300;
        constexpr int MenuCloseTab = 1;

        int ToolbarHeight() { return ui::Scale(48); }
        int SplitterWidth() { return ui::Scale(6); }
    }

    MainWindow::MainWindow() : m_files(*this) {}
    MainWindow::~MainWindow() = default;

    bool MainWindow::Create(HINSTANCE instance, const fs::path& assets, int show)
    {
        m_assets = assets;
        WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
        wc.lpfnWndProc = Proc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
        wc.lpszClassName = ClassName;
        RegisterClassExW(&wc);

        const std::wstring title = L"Pluto Fall Dialogue Editor - " + assets.wstring();
        m_hwnd = CreateWindowExW(WS_EX_CONTROLPARENT, ClassName, title.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, instance, this);
        if (!m_hwnd)
            return false;

        // Size the window for the monitor it opened on.
        ui::SetDpi(GetDpiForWindow(m_hwnd));
        ui::ApplyFonts(m_hwnd);
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        const RECT& work = mi.rcWork;
        const int w = std::min(ui::Scale(1500), static_cast<int>(work.right - work.left));
        const int h = std::min(ui::Scale(980), static_cast<int>(work.bottom - work.top));
        SetWindowPos(m_hwnd, nullptr, work.left + (work.right - work.left - w) / 2, work.top + (work.bottom - work.top - h) / 2,
                     w, h, SWP_NOZORDER);
        m_leftWidth = ui::Scale(300);
        Layout();
        ShowWindow(m_hwnd, show);
        UpdateWindow(m_hwnd);
        SetFocus(m_files.Hwnd());
        return true;
    }

    LRESULT CALLBACK MainWindow::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == WM_NCCREATE)
        {
            auto* self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->m_hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        return self ? self->Handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
    }

    LRESULT MainWindow::Handle(UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_CREATE:
        {
            ui::SetDpi(GetDpiForWindow(m_hwnd));
            m_saveButton = ui::IconButton(m_hwnd, ui::Glyph::Save, L"Save", CommandId::Save);
            m_refreshButton = ui::IconButton(m_hwnd, ui::Glyph::Refresh, L"Reload list", CommandId::Refresh);
            m_assetsLabel = ui::Label(m_hwnd, L"Assets: " + m_assets.wstring(), ui::SmallFont());
            m_files.Create(m_hwnd);
            m_tabs = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP, 0, 0, 10, 10,
                                     m_hwnd, reinterpret_cast<HMENU>(IdTabs), ui::Instance(), nullptr);
            SendMessageW(m_tabs, WM_SETFONT, reinterpret_cast<WPARAM>(ui::Font()), FALSE);
            m_noTabsHint = ui::Label(m_hwnd, L"Double-click a dialogue on the left (or right-click it and choose Open) to edit it.");
            return 0;
        }
        case WM_SIZE:
            Layout();
            return 0;
        case WM_GETMINMAXINFO:
            reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = { ui::Scale(900), ui::Scale(500) };
            return 0;
        case WM_DPICHANGED:
        {
            const UINT oldDpi = ui::Dpi();
            ui::SetDpi(HIWORD(wp));
            m_leftWidth = MulDiv(m_leftWidth, static_cast<int>(ui::Dpi()), static_cast<int>(oldDpi));
            ui::ApplyFonts(m_hwnd);
            const RECT* suggested = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(m_hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            m_files.OnDpiChanged();
            for (auto& doc : m_documents)
                doc->OnDpiChanged();
            Layout();
            return 0;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(m_hwnd, &ps);
            RECT rc;
            GetClientRect(m_hwnd, &rc);
            RECT line{ 0, ToolbarHeight() - 1, rc.right, ToolbarHeight() };
            FillRect(hdc, &line, GetSysColorBrush(COLOR_3DLIGHT));
            RECT split{ m_leftWidth, ToolbarHeight(), m_leftWidth + SplitterWidth(), rc.bottom };
            FillRect(hdc, &split, GetSysColorBrush(COLOR_3DFACE));
            EndPaint(m_hwnd, &ps);
            return 0;
        }
        case WM_CTLCOLORSTATIC:
            SetBkColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_WINDOW));
            SetTextColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_GRAYTEXT));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        case WM_DRAWITEM:
            ui::DrawIconButton(*reinterpret_cast<const DRAWITEMSTRUCT*>(lp));
            return TRUE;
        case WM_SETCURSOR:
            if (reinterpret_cast<HWND>(wp) == m_hwnd && LOWORD(lp) == HTCLIENT)
            {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(m_hwnd, &pt);
                if (OverSplitter(pt.x) && pt.y > ToolbarHeight())
                {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                    return TRUE;
                }
            }
            break;
        case WM_LBUTTONDOWN:
            if (OverSplitter(GET_X_LPARAM(lp)))
            {
                m_dragging = true;
                SetCapture(m_hwnd);
            }
            return 0;
        case WM_MOUSEMOVE:
            if (m_dragging)
            {
                RECT rc;
                GetClientRect(m_hwnd, &rc);
                m_leftWidth = std::clamp(static_cast<int>(GET_X_LPARAM(lp)), ui::Scale(200),
                                         std::max(ui::Scale(200), static_cast<int>(rc.right) - ui::Scale(500)));
                Layout();
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
        case WM_CAPTURECHANGED:
            if (m_dragging)
            {
                m_dragging = false;
                if (msg == WM_LBUTTONUP)
                    ReleaseCapture();
            }
            return 0;
        case WM_NOTIFY:
        {
            const auto* hdr = reinterpret_cast<const NMHDR*>(lp);
            if (hdr->hwndFrom == m_tabs)
            {
                if (hdr->code == TCN_SELCHANGE)
                {
                    SelectTab(TabCtrl_GetCurSel(m_tabs));
                }
                else if (hdr->code == NM_RCLICK)
                {
                    TCHITTESTINFO hit{};
                    GetCursorPos(&hit.pt);
                    POINT screen = hit.pt;
                    ScreenToClient(m_tabs, &hit.pt);
                    const int index = TabCtrl_HitTest(m_tabs, &hit);
                    if (index >= 0)
                    {
                        HMENU menu = CreatePopupMenu();
                        AppendMenuW(menu, MF_STRING, MenuCloseTab, L"&Close\tCtrl+W");
                        if (TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, m_hwnd, nullptr) ==
                            MenuCloseTab)
                            CloseTab(index, true);
                        DestroyMenu(menu);
                    }
                    return TRUE;
                }
            }
            break;
        }
        case WM_COMMAND:
            switch (LOWORD(wp))
            {
            case CommandId::Save: SaveAll(); return 0;
            case CommandId::CloseTab:
                if (m_current >= 0)
                    CloseTab(m_current, true);
                return 0;
            case CommandId::Refresh: m_files.Refresh(); return 0;
            case CommandId::NewDialog: NewDialog(); return 0;
            default: break;
            }
            break;
        case WM_CLOSE:
            if (HasUnsavedWork())
            {
                const int answer = MessageBoxW(m_hwnd,
                                               L"You have unsaved changes or files marked for deletion.\n\n"
                                               L"Save before closing?",
                                               L"Pluto Fall Dialogue Editor", MB_YESNOCANCEL | MB_ICONWARNING);
                if (answer == IDCANCEL)
                    return 0;
                if (answer == IDYES && !SaveAll())
                    return 0;
            }
            DestroyWindow(m_hwnd);
            return 0;
        case WM_DESTROY:
            m_documents.clear();
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(m_hwnd, msg, wp, lp);
    }

    bool MainWindow::OverSplitter(int x) const
    {
        return x >= m_leftWidth && x < m_leftWidth + SplitterWidth();
    }

    void MainWindow::Layout()
    {
        if (!m_tabs)
            return;
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        const int bar = ToolbarHeight();
        const int m = ui::Scale(8);
        MoveWindow(m_saveButton, m, m, ui::Scale(96), bar - 2 * m, TRUE);
        MoveWindow(m_refreshButton, m + ui::Scale(104), m, ui::Scale(128), bar - 2 * m, TRUE);
        MoveWindow(m_assetsLabel, m + ui::Scale(248), m + ui::Scale(8), std::max(0, static_cast<int>(rc.right) - ui::Scale(260)),
                   ui::Scale(20), TRUE);

        m_leftWidth = std::clamp(m_leftWidth, ui::Scale(200), std::max(ui::Scale(200), static_cast<int>(rc.right) - ui::Scale(500)));
        MoveWindow(m_files.Hwnd(), 0, bar, m_leftWidth, std::max(0, static_cast<int>(rc.bottom) - bar), TRUE);

        const int right = m_leftWidth + SplitterWidth();
        RECT tabArea{ right, bar + ui::Scale(4), rc.right - ui::Scale(4), rc.bottom - ui::Scale(4) };
        MoveWindow(m_tabs, tabArea.left, tabArea.top, std::max(0, static_cast<int>(tabArea.right - tabArea.left)),
                   std::max(0, static_cast<int>(tabArea.bottom - tabArea.top)), TRUE);
        RECT page = tabArea;
        TabCtrl_AdjustRect(m_tabs, FALSE, &page);
        for (auto& doc : m_documents)
            doc->SetBounds(page);

        MoveWindow(m_noTabsHint, page.left + ui::Scale(20), page.top + ui::Scale(20),
                   std::max(0, static_cast<int>(page.right - page.left) - ui::Scale(40)), ui::Scale(40), TRUE);
        ShowWindow(m_noTabsHint, m_documents.empty() ? SW_SHOW : SW_HIDE);
        if (m_documents.empty())
            SetWindowPos(m_noTabsHint, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }

    // ------------------------------------------------------------------ tabs

    int MainWindow::FindTab(const fs::path& file) const
    {
        for (size_t i = 0; i < m_documents.size(); ++i)
        {
            if (EqualsNoCase(m_documents[i]->Path().wstring(), file.wstring()))
                return static_cast<int>(i);
        }
        return -1;
    }

    void MainWindow::SelectTab(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_documents.size()))
        {
            m_current = -1;
            Layout();
            return;
        }
        m_current = index;
        TabCtrl_SetCurSel(m_tabs, index);
        for (size_t i = 0; i < m_documents.size(); ++i)
            m_documents[i]->Show(static_cast<int>(i) == index);
        m_files.SelectFile(m_documents[static_cast<size_t>(index)]->Path());
    }

    void MainWindow::UpdateTabText(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_documents.size()))
            return;
        const DocumentView& doc = *m_documents[static_cast<size_t>(index)];
        std::wstring text = doc.Path().filename().wstring();
        if (doc.Modified())
            text += L" *";
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = text.data();
        TabCtrl_SetItem(m_tabs, index, &item);
    }

    void MainWindow::OpenDialog(const fs::path& file)
    {
        if (const int existing = FindTab(file); existing >= 0)
        {
            SelectTab(existing);
            return;
        }
        auto view = std::make_unique<DocumentView>(*this);
        std::wstring error;
        if (!view->Load(file, error))
        {
            MessageBoxW(m_hwnd, error.c_str(), L"Open dialogue", MB_ICONERROR);
            return;
        }
        view->Create(m_hwnd);
        const int index = static_cast<int>(m_documents.size());
        m_documents.push_back(std::move(view));
        std::wstring text = file.filename().wstring();
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = text.data();
        TabCtrl_InsertItem(m_tabs, index, &item);
        Layout();
        SelectTab(index);
    }

    bool MainWindow::CloseTab(int index, bool prompt)
    {
        if (index < 0 || index >= static_cast<int>(m_documents.size()))
            return false;
        DocumentView& doc = *m_documents[static_cast<size_t>(index)];
        if (prompt && doc.Modified())
        {
            const std::wstring message = L"Save changes to " + doc.Path().filename().wstring() + L"?";
            const int answer = MessageBoxW(m_hwnd, message.c_str(), L"Close", MB_YESNOCANCEL | MB_ICONQUESTION);
            if (answer == IDCANCEL)
                return false;
            if (answer == IDYES)
            {
                std::wstring error;
                if (!doc.Save(error))
                {
                    MessageBoxW(m_hwnd, (L"Could not save:\n" + error).c_str(), L"Save", MB_ICONERROR);
                    return false;
                }
            }
        }
        m_documents.erase(m_documents.begin() + index);
        TabCtrl_DeleteItem(m_tabs, index);
        if (m_documents.empty())
            m_current = -1;
        else
            SelectTab(std::min(index, static_cast<int>(m_documents.size()) - 1));
        Layout();
        return true;
    }

    void MainWindow::DocumentChanged(DocumentView& view)
    {
        for (size_t i = 0; i < m_documents.size(); ++i)
        {
            if (m_documents[i].get() == &view)
                UpdateTabText(static_cast<int>(i));
        }
        m_files.SetTitle(view.Path(), view.Title());
    }

    bool MainWindow::HasUnsavedWork() const
    {
        if (!m_files.MarkedForDeletion().empty())
            return true;
        return std::ranges::any_of(m_documents, [](const auto& d) { return d->Modified(); });
    }

    void MainWindow::AfterKeyboardNavigation()
    {
        if (m_current >= 0)
            m_documents[static_cast<size_t>(m_current)]->EnsureFocusVisible();
    }

    // ------------------------------------------------------------------ file actions

    void MainWindow::NewDialog()
    {
        const fs::path folder = DialogFolder();
        const auto name = ui::Prompt(m_hwnd, L"New dialogue", L"File name for the new dialogue:", L"new_dialogue.json",
                                     [&](std::wstring& value) -> std::wstring
                                     {
                                         std::wstring problem = ValidateDialogFileName(value);
                                         if (!problem.empty())
                                             return problem;
                                         std::error_code ec;
                                         if (fs::exists(folder / value, ec))
                                             return L"A file named " + value + L" already exists.";
                                         return {};
                                     });
        if (!name)
            return;
        std::error_code ec;
        fs::create_directories(folder, ec);
        const fs::path file = folder / *name;
        std::wstring error;
        if (!WriteFileText(file, model::NewDialogText(), error))
        {
            MessageBoxW(m_hwnd, (L"Could not create the file:\n" + error).c_str(), L"New dialogue", MB_ICONERROR);
            return;
        }
        m_files.Refresh();
        m_files.SelectFile(file);
        OpenDialog(file);
    }

    void MainWindow::RenameDialog(const fs::path& file)
    {
        const fs::path folder = file.parent_path();
        const auto name = ui::Prompt(m_hwnd, L"Rename dialogue", L"New file name:", file.filename().wstring(),
                                     [&](std::wstring& value) -> std::wstring
                                     {
                                         std::wstring problem = ValidateDialogFileName(value);
                                         if (!problem.empty())
                                             return problem;
                                         std::error_code ec;
                                         const fs::path target = folder / value;
                                         if (!EqualsNoCase(value, file.filename().wstring()) && fs::exists(target, ec))
                                             return L"A file named " + value + L" already exists.";
                                         return {};
                                     });
        if (!name || *name == file.filename().wstring())
            return;
        const fs::path target = folder / *name;
        if (!MoveFileExW(file.c_str(), target.c_str(), 0))
        {
            MessageBoxW(m_hwnd, (L"Could not rename the file:\n" + LastErrorMessage()).c_str(), L"Rename", MB_ICONERROR);
            return;
        }
        if (const int tab = FindTab(file); tab >= 0)
        {
            m_documents[static_cast<size_t>(tab)]->SetPath(target);
            UpdateTabText(tab);
        }
        m_files.FileRenamed(file, target);
    }

    bool MainWindow::SaveAll()
    {
        // Confirm deletions first. "No" leaves both the files and the open documents untouched.
        const std::vector<fs::path> marked = m_files.MarkedForDeletion();
        if (!marked.empty())
        {
            std::wstring message = L"The following files will be permanently deleted:\n\n";
            for (const auto& file : marked)
                message += L"    " + file.filename().wstring() + L"\n";
            message += L"\nAre you sure you want to delete them?";
            if (MessageBoxW(m_hwnd, message.c_str(), L"Delete dialogue files", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
                return false;
        }

        std::wstring problems;
        for (const auto& file : marked)
        {
            if (const int tab = FindTab(file); tab >= 0)
                CloseTab(tab, false); // the file is going away; discard its edits
            if (!DeleteFileW(file.c_str()))
                problems += L"Could not delete " + file.filename().wstring() + L": " + LastErrorMessage() + L"\n";
        }

        for (size_t i = 0; i < m_documents.size(); ++i)
        {
            DocumentView& doc = *m_documents[i];
            if (!doc.Modified())
                continue;
            std::wstring error;
            if (doc.Save(error))
                UpdateTabText(static_cast<int>(i));
            else
                problems += L"Could not save " + doc.Path().filename().wstring() + L": " + error + L"\n";
        }

        m_files.Refresh();
        if (!problems.empty())
        {
            MessageBoxW(m_hwnd, problems.c_str(), L"Save", MB_ICONERROR);
            return false;
        }
        return true;
    }
}
