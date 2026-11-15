#include "Screens.h"
#include "Game.h"

namespace pf
{
    int MenuList::Navigate(Game& game, int itemCount, float dt)
    {
        if (itemCount <= 0)
            return -1;
        Input& input = game.Controls();
        int direction = 0;
        if (input.AnyDown(Button::Up))
            direction = -1;
        else if (input.AnyDown(Button::Down))
            direction = 1;

        bool move = false;
        if (direction != 0 && direction != m_heldDirection)
        {
            move = true;
            m_repeat = 0.4f; // initial repeat delay
        }
        else if (direction != 0)
        {
            m_repeat -= dt;
            if (m_repeat <= 0.0f)
            {
                move = true;
                m_repeat = 0.12f;
            }
        }
        m_heldDirection = direction;

        if (!move)
            return -1;
        selected = (selected + direction + itemCount) % itemCount;
        game.PlayMenuMove();
        return direction;
    }

    void MenuList::Render(Ui& ui, std::span<const std::wstring> items, float centerX, float top, float spacing, float size,
                          float time) const
    {
        for (size_t i = 0; i < items.size(); ++i)
        {
            const bool active = static_cast<int>(i) == selected;
            const float y = top + spacing * static_cast<float>(i);
            const float width = 900.0f;
            D2D1_RECT_F rect{ centerX - width * 0.5f, y, centerX + width * 0.5f, y + spacing };
            if (active)
            {
                const float pulse = 0.55f + 0.25f * std::sin(time * 5.0f);
                D2D1_RECT_F highlight{ centerX - 330.0f, y + 4.0f, centerX + 330.0f, y + spacing - 4.0f };
                ui.FillRoundRect(highlight, 12.0f, Color{ 0.15f, 0.45f, 0.8f, pulse * 0.6f });
                ui.DrawRoundRect(highlight, 12.0f, Color{ 0.6f, 0.85f, 1.0f, pulse }, 2.0f);
            }
            TextStyle style;
            style.size = size;
            style.align = TextAlign::Center;
            style.verticalCenter = true;
            style.bold = active;
            style.shadow = true;
            style.color = active ? Colors::White : Color{ 0.78f, 0.82f, 0.9f, 1.0f };
            ui.Text(items[i], rect, style);
        }
    }

    void DrawPanel(Ui& ui, const D2D1_RECT_F& rect, float alpha)
    {
        ui.FillRoundRect(rect, 16.0f, Color{ 0.02f, 0.04f, 0.1f, alpha });
        ui.DrawRoundRect(rect, 16.0f, Color{ 0.45f, 0.7f, 1.0f, 0.85f }, 2.5f);
    }
}
