// MainWindow.h - the editor's top-level window: toolbar (Save), the file list on the left,
// a splitter, and tabs of open dialogs on the right.
#pragma once
#include "Common.h"
#include "FileListPane.h"

namespace de
{
    class DocumentView;

    namespace CommandId
    {
        constexpr int Save = 40001;
        constexpr int CloseTab = 40002;
        constexpr int Refresh = 40003;
        constexpr int NewDialog = 40004;
    }

    class MainWindow
    {
    public:
        MainWindow();
        ~MainWindow();

        bool Create(HINSTANCE instance, const fs::path& assets, int show);
        HWND Hwnd() const noexcept { return m_hwnd; }

        const fs::path& AssetsFolder() const noexcept { return m_assets; }
        fs::path DialogFolder() const { return m_assets / L"dialog"; }

        // Actions used by the panes.
        void OpenDialog(const fs::path& file);
        void RenameDialog(const fs::path& file);
        void NewDialog();
        bool SaveAll();
        void DocumentChanged(DocumentView& view);

        // Called by the message loop after keyboard navigation.
        void AfterKeyboardNavigation();

    private:
        static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
        LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
        void Layout();
        void SelectTab(int index);
        bool CloseTab(int index, bool prompt);
        int FindTab(const fs::path& file) const;
        void UpdateTabText(int index);
        bool HasUnsavedWork() const;
        bool OverSplitter(int x) const;

        HWND m_hwnd = nullptr;
        fs::path m_assets;
        FileListPane m_files;
        HWND m_saveButton = nullptr;
        HWND m_refreshButton = nullptr;
        HWND m_assetsLabel = nullptr;
        HWND m_tabs = nullptr;
        HWND m_noTabsHint = nullptr;
        std::vector<std::unique_ptr<DocumentView>> m_documents; // in tab order
        int m_current = -1;
        int m_leftWidth = 0;
        bool m_dragging = false;
    };
}
