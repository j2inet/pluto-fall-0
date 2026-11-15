// Screens.h - factories for every screen plus a shared vertical menu widget.
#pragma once
#include "Common.h"

namespace pf
{
    class Game;
    class Screen;
    class Ui;

    std::unique_ptr<Screen> MakeTitleScreen(Game& game);
    std::unique_ptr<Screen> MakeOptionsScreen(Game& game);
    std::unique_ptr<Screen> MakeCreditsScreen(Game& game);
    std::unique_ptr<Screen> MakePlayerSelectScreen(Game& game);
    std::unique_ptr<Screen> MakeDialogScreen(Game& game, const std::string& dialogPath);
    std::unique_ptr<Screen> MakeGameplayScreen(Game& game, const std::string& levelPath);

    // Up/down navigation with key repeat, shared by the title, options and pause menus.
    class MenuList
    {
    public:
        int selected = 0;

        // Returns -1 for none, otherwise the direction moved (+1 down / -1 up).
        int Navigate(Game& game, int itemCount, float dt);

        // Draws centred menu items. Highlights the selected one.
        void Render(Ui& ui, std::span<const std::wstring> items, float centerX, float top, float spacing, float size,
                    float time) const;

    private:
        float m_repeat = 0.0f;
        int m_heldDirection = 0;
    };

    // Draws a translucent panel with a border; used by several screens.
    void DrawPanel(Ui& ui, const D2D1_RECT_F& rect, float alpha = 0.72f);
}
