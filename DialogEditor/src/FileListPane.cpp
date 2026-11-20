#include "FileListPane.h"
#include "DialogModel.h"
#include "MainWindow.h"
#include "Ui.h"
#include "Util.h"

namespace de
{
    namespace
    {
        constexpr wchar_t ClassName[] = L"DE_FileListPane";
        constexpr int IdNew = 100;
        constexpr int IdList = 101;
        constexpr int MenuOpen = 1, MenuRename = 2, MenuDelete = 3, MenuUndelete = 4;

        std::wstring ReadTitle(const fs::path& file)
        {
            auto text = ReadFileText(file);
            if (!text)
                return {};
            json::Document doc;
            std::string error;
            if (!doc.Parse(*text, error))
                return L"(could not parse: " + Widen(error) + L")";
            return Widen(model::DisplayLoc(doc.Root(), "title", "en"));
        }
    }

    HWND FileListPane::Create(HWND parent)
    {
        static bool registered = false;
        if (!registered)
        {
            WNDCLASSEXW wc{ sizeof(WNDCLASSEXW) };
            wc.lpfnWndProc = Proc;
            wc.hInstance = ui::Instance();
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
            wc.lpszClassName = ClassName;
            RegisterClassExW(&wc);
            registered = true;
        }
        m_hwnd = CreateWindowExW(WS_EX_CONTROLPARENT, ClassName, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 10,
                                 10, parent, nullptr, ui::Instance(), this);
        m_header = ui::Label(m_hwnd, L"Dialogues", ui::BoldFont());
        m_newButton = ui::IconButton(m_hwnd, ui::Glyph::Add, L"New dialogue", IdNew);
        m_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTBOXW, L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED |
                                     LBS_NOINTEGRALHEIGHT,
                                 0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(IdList), ui::Instance(), nullptr);
        Refresh();
        return m_hwnd;
    }

    LRESULT CALLBACK FileListPane::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == WM_NCCREATE)
        {
            auto* self = static_cast<FileListPane*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->m_hwnd = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        auto* self = reinterpret_cast<FileListPane*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        return self ? self->Handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
    }

    LRESULT FileListPane::Handle(UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_SIZE:
            Layout();
            return 0;
        case WM_MEASUREITEM:
            reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->itemHeight = static_cast<UINT>(ui::Scale(46));
            return TRUE;
        case WM_DRAWITEM:
        {
            const auto& dis = *reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
            if (dis.CtlType == ODT_LISTBOX)
                Draw(dis);
            else
                ui::DrawIconButton(dis);
            return TRUE;
        }
        case WM_CTLCOLORSTATIC:
            SetBkColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        case WM_COMMAND:
            if (LOWORD(wp) == IdNew && HIWORD(wp) == BN_CLICKED)
            {
                m_main.NewDialog();
                return 0;
            }
            if (LOWORD(wp) == IdList && HIWORD(wp) == LBN_DBLCLK)
            {
                if (Item* item = Selected())
                    m_main.OpenDialog(item->path);
                return 0;
            }
            break;
        case WM_CONTEXTMENU:
            if (reinterpret_cast<HWND>(wp) == m_list)
            {
                ShowContextMenu(lp);
                return 0;
            }
            break;
        default:
            break;
        }
        return DefWindowProcW(m_hwnd, msg, wp, lp);
    }

    void FileListPane::Layout()
    {
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        const int m = ui::Scale(10);
        const int w = rc.right - 2 * m;
        MoveWindow(m_header, m, m, w, ui::Scale(24), TRUE);
        MoveWindow(m_newButton, m, m + ui::Scale(30), w, ui::Scale(32), TRUE);
        const int top = m + ui::Scale(70);
        MoveWindow(m_list, m, top, w, std::max(0, static_cast<int>(rc.bottom) - top - m), TRUE);
    }

    void FileListPane::OnDpiChanged()
    {
        SendMessageW(m_list, LB_SETITEMHEIGHT, 0, ui::Scale(46));
        Layout();
    }

    void FileListPane::Refresh()
    {
        std::set<std::wstring> marked;
        for (const Item& item : m_items)
            if (item.marked)
                marked.insert(item.path.wstring());
        fs::path selected;
        if (Item* item = Selected())
            selected = item->path;

        m_items.clear();
        std::error_code ec;
        const fs::path folder = m_main.DialogFolder();
        if (fs::is_directory(folder, ec))
        {
            for (const auto& entry : fs::directory_iterator(folder, ec))
            {
                std::wstring ext = entry.path().extension().wstring();
                if (!entry.is_regular_file(ec) || !EqualsNoCase(ext, L".json"))
                    continue;
                Item item;
                item.path = entry.path();
                item.name = entry.path().filename().wstring();
                item.title = ReadTitle(entry.path());
                item.marked = marked.contains(item.path.wstring());
                m_items.push_back(std::move(item));
            }
        }
        std::ranges::sort(m_items, [](const Item& a, const Item& b)
        {
            return CompareStringOrdinal(a.name.c_str(), -1, b.name.c_str(), -1, TRUE) == CSTR_LESS_THAN;
        });

        SendMessageW(m_list, WM_SETREDRAW, FALSE, 0);
        SendMessageW(m_list, LB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < m_items.size(); ++i)
            SendMessageW(m_list, LB_ADDSTRING, 0, static_cast<LPARAM>(i));
        SendMessageW(m_list, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(m_list, nullptr, TRUE);
        if (!selected.empty())
            SelectFile(selected);
    }

    void FileListPane::SelectFile(const fs::path& file)
    {
        const int index = Find(file);
        if (index >= 0)
            SendMessageW(m_list, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
    }

    int FileListPane::Find(const fs::path& file) const
    {
        for (size_t i = 0; i < m_items.size(); ++i)
        {
            if (EqualsNoCase(m_items[i].path.wstring(), file.wstring()))
                return static_cast<int>(i);
        }
        return -1;
    }

    FileListPane::Item* FileListPane::Selected()
    {
        const auto index = static_cast<int>(SendMessageW(m_list, LB_GETCURSEL, 0, 0));
        if (index < 0 || index >= static_cast<int>(m_items.size()))
            return nullptr;
        return &m_items[static_cast<size_t>(index)];
    }

    std::vector<fs::path> FileListPane::MarkedForDeletion() const
    {
        std::vector<fs::path> files;
        for (const Item& item : m_items)
            if (item.marked)
                files.push_back(item.path);
        return files;
    }

    bool FileListPane::IsMarked(const fs::path& file) const
    {
        const int index = Find(file);
        return index >= 0 && m_items[static_cast<size_t>(index)].marked;
    }

    void FileListPane::SetTitle(const fs::path& file, const std::wstring& title)
    {
        const int index = Find(file);
        if (index < 0 || m_items[static_cast<size_t>(index)].title == title)
            return;
        m_items[static_cast<size_t>(index)].title = title;
        RECT rc;
        if (SendMessageW(m_list, LB_GETITEMRECT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&rc)) != LB_ERR)
            InvalidateRect(m_list, &rc, TRUE);
    }

    void FileListPane::FileRenamed(const fs::path& from, const fs::path& to)
    {
        const int index = Find(from);
        const bool marked = index >= 0 && m_items[static_cast<size_t>(index)].marked;
        Refresh();
        const int renamed = Find(to);
        if (renamed >= 0)
        {
            m_items[static_cast<size_t>(renamed)].marked = marked;
            SelectFile(to);
        }
    }

    void FileListPane::SetMarked(int index, bool marked)
    {
        if (index < 0 || index >= static_cast<int>(m_items.size()))
            return;
        m_items[static_cast<size_t>(index)].marked = marked;
        RECT rc;
        if (SendMessageW(m_list, LB_GETITEMRECT, static_cast<WPARAM>(index), reinterpret_cast<LPARAM>(&rc)) != LB_ERR)
            InvalidateRect(m_list, &rc, TRUE);
    }

    void FileListPane::ShowContextMenu(LPARAM lp)
    {
        POINT screen{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int index = -1;
        if (screen.x == -1 && screen.y == -1)
        {
            // Keyboard (Shift+F10 / menu key): use the selected item.
            index = static_cast<int>(SendMessageW(m_list, LB_GETCURSEL, 0, 0));
            RECT rc{};
            SendMessageW(m_list, LB_GETITEMRECT, static_cast<WPARAM>(std::max(index, 0)), reinterpret_cast<LPARAM>(&rc));
            screen = { rc.left + ui::Scale(20), rc.bottom };
            ClientToScreen(m_list, &screen);
        }
        else
        {
            POINT client = screen;
            ScreenToClient(m_list, &client);
            const auto hit = static_cast<DWORD>(SendMessageW(m_list, LB_ITEMFROMPOINT, 0, MAKELPARAM(client.x, client.y)));
            if (HIWORD(hit) == 0)
            {
                index = LOWORD(hit);
                SendMessageW(m_list, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
            }
        }
        if (index < 0 || index >= static_cast<int>(m_items.size()))
            return;

        const Item item = m_items[static_cast<size_t>(index)];
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, MenuOpen, L"&Open");
        AppendMenuW(menu, MF_STRING, MenuRename, L"&Rename...");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        if (item.marked)
            AppendMenuW(menu, MF_STRING, MenuUndelete, L"&Undelete");
        else
            AppendMenuW(menu, MF_STRING, MenuDelete, L"&Delete");
        SetMenuDefaultItem(menu, MenuOpen, FALSE);
        const int choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, m_hwnd, nullptr);
        DestroyMenu(menu);

        switch (choice)
        {
        case MenuOpen: m_main.OpenDialog(item.path); break;
        case MenuRename: m_main.RenameDialog(item.path); break;
        case MenuDelete: SetMarked(index, true); break;
        case MenuUndelete: SetMarked(index, false); break;
        default: break;
        }
    }

    void FileListPane::Draw(const DRAWITEMSTRUCT& dis)
    {
        HDC hdc = dis.hDC;
        RECT rc = dis.rcItem;
        const bool selected = (dis.itemState & ODS_SELECTED) != 0;
        FillRect(hdc, &rc, GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
        if (dis.itemID == static_cast<UINT>(-1) || dis.itemData >= m_items.size())
            return;
        const Item& item = m_items[dis.itemData];

        SetBkMode(hdc, TRANSPARENT);
        const int pad = ui::Scale(8);
        const int iconWidth = ui::Scale(28);
        RECT text{ rc.left + pad, rc.top, rc.right - pad - (item.marked ? iconWidth : 0), rc.bottom };

        const COLORREF primary = GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);
        const COLORREF secondary = selected ? GetSysColor(COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_GRAYTEXT);

        HGDIOBJ old = SelectObject(hdc, ui::Font());
        SetTextColor(hdc, item.marked ? secondary : primary);
        if (item.title.empty())
        {
            DrawTextW(hdc, item.name.c_str(), -1, &text, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        }
        else
        {
            RECT top = text;
            top.bottom = rc.top + (rc.bottom - rc.top) / 2 + ui::Scale(2);
            DrawTextW(hdc, item.name.c_str(), -1, &top, DT_SINGLELINE | DT_BOTTOM | DT_END_ELLIPSIS | DT_NOPREFIX);
            RECT bottom = text;
            bottom.top = top.bottom;
            SelectObject(hdc, ui::SmallFont());
            SetTextColor(hdc, secondary);
            DrawTextW(hdc, item.title.c_str(), -1, &bottom, DT_SINGLELINE | DT_TOP | DT_END_ELLIPSIS | DT_NOPREFIX);
        }

        if (item.marked)
        {
            // Trash can: this file will be deleted on Save.
            SelectObject(hdc, ui::IconFont());
            SetTextColor(hdc, selected ? GetSysColor(COLOR_HIGHLIGHTTEXT) : RGB(196, 43, 28));
            RECT icon{ rc.right - pad - iconWidth, rc.top, rc.right - pad, rc.bottom };
            const wchar_t glyph[2] = { ui::Glyph::Delete, 0 };
            DrawTextW(hdc, glyph, 1, &icon, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
        }
        SelectObject(hdc, old);

        if (dis.itemState & ODS_FOCUS)
            DrawFocusRect(hdc, &rc);
    }
}
