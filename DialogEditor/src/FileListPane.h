// FileListPane.h - left pane: the list of dialog files, the "New dialogue" button and the
// per-file context menu (Open, Rename, Delete / Undelete).
#pragma once
#include "Common.h"

namespace de
{
    class MainWindow;

    class FileListPane
    {
    public:
        explicit FileListPane(MainWindow& main) : m_main(main) {}

        HWND Create(HWND parent);
        HWND Hwnd() const noexcept { return m_hwnd; }

        // Rescans <assets>/dialog, keeping deletion marks and the selection.
        void Refresh();
        void SelectFile(const fs::path& file);

        std::vector<fs::path> MarkedForDeletion() const;
        bool IsMarked(const fs::path& file) const;
        void SetTitle(const fs::path& file, const std::wstring& title);
        void FileRenamed(const fs::path& from, const fs::path& to);
        void OnDpiChanged();

    private:
        struct Item
        {
            fs::path path;
            std::wstring name;
            std::wstring title;
            bool marked = false;
        };

        static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
        LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
        void Layout();
        void Draw(const DRAWITEMSTRUCT& dis);
        void ShowContextMenu(LPARAM lp);
        int Find(const fs::path& file) const;
        void SetMarked(int index, bool marked);
        Item* Selected();

        MainWindow& m_main;
        HWND m_hwnd = nullptr;
        HWND m_header = nullptr;
        HWND m_newButton = nullptr;
        HWND m_list = nullptr;
        std::vector<Item> m_items;
    };
}
