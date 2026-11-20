// DocumentView.h - the editor for one open dialog file (one tab in the right pane).
//
// Layout, top to bottom, inside a vertically scrolling panel:
//   language selector (+ add language) and title
//   segments list (add / remove / reorder) | selected segment: id, terminal, background image
//   entries list (add / remove / reorder)
//   selected entry: speaker, text, portrait, question key, responses (id, text, goto)
//   next levels (key, level)
#pragma once
#include "Common.h"
#include "LosslessJson.h"

namespace de
{
    class MainWindow;

    class DocumentView
    {
    public:
        explicit DocumentView(MainWindow& main) : m_main(main) {}
        ~DocumentView();
        DocumentView(const DocumentView&) = delete;
        DocumentView& operator=(const DocumentView&) = delete;

        bool Load(const fs::path& file, std::wstring& error);
        void Create(HWND parent);

        HWND Hwnd() const noexcept { return m_viewport; }
        const fs::path& Path() const noexcept { return m_path; }
        void SetPath(const fs::path& path) { m_path = path; }
        bool Modified() const noexcept { return m_modified; }
        bool Save(std::wstring& error);
        // Root "title" for display in the file list.
        std::wstring Title() const;

        void SetBounds(const RECT& bounds);
        void Show(bool visible);
        void OnDpiChanged();
        // Scrolls so the control with keyboard focus is visible.
        void EnsureFocusVisible();

    private:
        using Node = json::Node;

        static LRESULT CALLBACK ViewportProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
        static LRESULT CALLBACK CanvasProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
        LRESULT HandleCanvas(UINT msg, WPARAM wp, LPARAM lp);
        void OnCommand(int id, int code, HWND control);
        void OnNotify(const NMHDR& hdr);
        void Layout();
        void ScrollTo(int position);
        void SetModified();
        HWND Owner() const;

        // Model access for the current selection.
        Node* Segment();
        Node* Entry();
        Node* Response();
        Node* NextLevel();

        // Refresh UI from the model.
        void RefreshAll();
        void RefreshLanguages(const std::string& select);
        void RefreshTitle();
        void RefreshSegments();
        void RefreshSegmentDetails();
        void RefreshEntries();
        void RefreshEntryDetails();
        void RefreshResponses();
        void RefreshResponseDetails();
        void RefreshGotoCombo();
        void RefreshNextLevels();
        void RefreshNextDetails();
        void RefreshLevelChoices();
        void UpdateEntryRow(int index);
        void UpdateResponseRow(int index);
        void UpdateNextRow(int index);
        void UpdateEnabled();

        // Commands.
        void AddLanguage();
        void AddSegment();
        void RemoveSegment();
        void MoveSegment(int delta);
        void AddEntry();
        void RemoveEntry();
        void MoveEntry(int delta);
        void AddResponse();
        void RemoveResponse();
        void MoveResponse(int delta);
        void AddNextLevel();
        void RemoveNextLevel();
        void MoveNextLevel(int delta);
        void ChooseImage(bool background);
        void ClearImage(bool background);
        void ShowImage(bool background);

        MainWindow& m_main;
        fs::path m_path;
        json::Document m_doc;
        bool m_modified = false;
        int m_updating = 0; // > 0 while the UI is being filled from the model

        std::string m_language = "en";
        std::set<std::string> m_addedLanguages;
        int m_segment = -1;
        int m_entry = -1;
        int m_response = -1;
        int m_next = -1;
        std::string m_idBeforeEdit;
        int m_scroll = 0;
        int m_contentHeight = 0;

        HWND m_viewport = nullptr;
        HWND m_canvas = nullptr;

        HWND m_langLabel = nullptr, m_langCombo = nullptr, m_langAdd = nullptr, m_titleLabel = nullptr, m_titleEdit = nullptr;
        HWND m_segHeader = nullptr, m_segList = nullptr, m_segAdd = nullptr, m_segRemove = nullptr, m_segUp = nullptr,
             m_segDown = nullptr;
        HWND m_idLabel = nullptr, m_idEdit = nullptr, m_terminal = nullptr, m_bgLabel = nullptr, m_bgThumb = nullptr,
             m_bgChange = nullptr, m_bgClear = nullptr, m_bgPath = nullptr;
        HWND m_entHeader = nullptr, m_entList = nullptr, m_entAdd = nullptr, m_entRemove = nullptr, m_entUp = nullptr,
             m_entDown = nullptr;
        HWND m_entryHeader = nullptr, m_speakerLabel = nullptr, m_speakerEdit = nullptr, m_textLabel = nullptr,
             m_textEdit = nullptr, m_portraitLabel = nullptr, m_portraitThumb = nullptr, m_portraitChange = nullptr,
             m_portraitClear = nullptr, m_portraitPath = nullptr, m_keyLabel = nullptr, m_keyEdit = nullptr,
             m_keyHint = nullptr;
        HWND m_respLabel = nullptr, m_respList = nullptr, m_respAdd = nullptr, m_respRemove = nullptr, m_respUp = nullptr,
             m_respDown = nullptr, m_respIdLabel = nullptr, m_respIdEdit = nullptr, m_respTextLabel = nullptr,
             m_respTextEdit = nullptr, m_respGotoLabel = nullptr, m_respGotoCombo = nullptr;
        HWND m_nextHeader = nullptr, m_nextList = nullptr, m_nextAdd = nullptr, m_nextRemove = nullptr, m_nextUp = nullptr,
             m_nextDown = nullptr, m_nextKeyLabel = nullptr, m_nextKeyEdit = nullptr, m_nextLevelLabel = nullptr,
             m_nextLevelCombo = nullptr, m_nextHint = nullptr;
    };
}
