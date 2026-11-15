// TitleScreen.cpp - background image, byline and the main menu.
#include "Game.h"
#include "Localization.h"
#include "Screens.h"

namespace pf
{
    namespace
    {
        class TitleScreen final : public Screen
        {
        public:
            using Screen::Screen;

            void Enter() override
            {
                m_game.Sound().PlayMusic(m_game.Data().Config().titleMusic);
            }

            void Update(float dt) override
            {
                m_time += dt;
                m_menu.Navigate(m_game, ItemCount, dt);
                if (!m_game.Controls().AnyConfirm())
                    return;

                m_game.PlayMenuSelect();
                switch (m_menu.selected)
                {
                case 0: m_game.ShowPlayerSelect(); break;
                case 1: m_game.ShowCredits(); break;
                case 2: m_game.OpenWebsite(); break;
                case 3: m_game.ShowOptions(); break;
                case 4: m_game.Quit(); break;
                default: break;
                }
            }

            void Render() override
            {
                Graphics& gfx = m_game.Gfx();
                Ui& ui = m_game.UI();
                gfx.Begin2D();

                // Slow "Ken Burns" drift over the title art.
                const float zoom = 1.04f + 0.03f * std::sin(m_time * 0.15f);
                const float w = Graphics::CanvasWidth * zoom, h = Graphics::CanvasHeight * zoom;
                const float ox = (Graphics::CanvasWidth - w) * 0.5f + 20.0f * std::sin(m_time * 0.1f);
                const float oy = (Graphics::CanvasHeight - h) * 0.5f;
                ui.Image(m_game.Data().Config().titleImage, D2D1::RectF(ox, oy, ox + w, oy + h), 1.0f, true);

                // Darken the lower half so the menu is readable.
                for (int i = 0; i < 12; ++i)
                {
                    const float y = 480.0f + i * 50.0f;
                    ui.FillRect(D2D1::RectF(0, y, Graphics::CanvasWidth, y + 50.0f), Color{ 0, 0, 0, 0.04f * i });
                }

                TextStyle title;
                title.size = 132.0f;
                title.bold = true;
                title.align = TextAlign::Center;
                title.shadow = true;
                title.color = Color{ 0.85f, 0.93f, 1.0f, 1.0f };
                ui.Text(Localization::GetW("title.name"), D2D1::RectF(0, 90, Graphics::CanvasWidth, 280), title);

                // "A game from Joel Ivory Johnson" overlaid on the title image.
                TextStyle byline;
                byline.size = 40.0f;
                byline.align = TextAlign::Center;
                byline.shadow = true;
                byline.color = Colors::Gold;
                ui.Text(Localization::GetW("title.byline"), D2D1::RectF(0, 270, Graphics::CanvasWidth, 340), byline);

                const std::array<std::wstring, ItemCount> items = {
                    Localization::GetW("menu.start"), Localization::GetW("menu.credits"),
                    Localization::GetW("menu.website"), Localization::GetW("menu.options"),
                    Localization::GetW("menu.quit"),
                };
                m_menu.Render(ui, items, Graphics::CanvasWidth * 0.5f, 520.0f, 82.0f, 46.0f, m_time);

                TextStyle hint;
                hint.size = 24.0f;
                hint.align = TextAlign::Center;
                hint.color = Color{ 0.8f, 0.85f, 0.95f, 0.8f };
                ui.Text(Localization::GetW("hint.menu"), D2D1::RectF(0, 1010, Graphics::CanvasWidth, 1060), hint);
                gfx.End2D();
            }

        private:
            static constexpr int ItemCount = 5;
            MenuList m_menu;
            float m_time = 0.0f;
        };
    }

    std::unique_ptr<Screen> MakeTitleScreen(Game& game)
    {
        return std::make_unique<TitleScreen>(game);
    }
}
