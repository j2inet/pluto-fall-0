// CreditsScreen.cpp - auto-scrolling credits loaded from JSON.
// Up/Down speed up or slow down the scroll; fire or menu returns to the title screen,
// as does the last line scrolling off the top.
#include "Game.h"
#include "Localization.h"
#include "Screens.h"
#include "Util.h"

namespace pf
{
    namespace
    {
        class CreditsScreen final : public Screen
        {
        public:
            using Screen::Screen;

            void Enter() override
            {
                m_sections = LoadCredits(m_game.Data().Config().credits);
                // Lay out the lines once.
                float y = 0.0f;
                for (const CreditSection& section : m_sections)
                {
                    m_lines.push_back({ section.title.GetW(), Colors::Gold, y, true });
                    y += HeaderHeight;
                    for (const CreditLine& line : section.contributors)
                    {
                        m_lines.push_back({ Widen(line.value), line.color, y, false });
                        y += LineHeight;
                    }
                    y += SectionGap;
                }
                m_totalHeight = y;
            }

            void Update(float dt) override
            {
                Input& input = m_game.Controls();
                if (input.AnyPressed(Button::Primary) || input.AnyPressed(Button::Menu))
                {
                    Finish();
                    return;
                }
                if (input.AnyDown(Button::Up))
                    m_speed = std::min(MaxSpeed, m_speed + 420.0f * dt);
                else if (input.AnyDown(Button::Down))
                    m_speed = std::max(MinSpeed, m_speed - 420.0f * dt);

                m_scroll += m_speed * dt;
                // Done once the bottom of the last item has passed the top edge.
                if (Graphics::CanvasHeight + m_totalHeight - m_scroll < 0.0f)
                    Finish();
            }

            void Render() override
            {
                Graphics& gfx = m_game.Gfx();
                Ui& ui = m_game.UI();
                gfx.Begin2D();
                ui.FillScreen(Color{ 0.0f, 0.0f, 0.02f, 1.0f });
                ui.Image(m_game.Data().Config().titleImage, D2D1::RectF(0, 0, Graphics::CanvasWidth, Graphics::CanvasHeight),
                         0.18f, true);

                for (const Line& line : m_lines)
                {
                    const float y = Graphics::CanvasHeight + line.y - m_scroll;
                    const float height = line.header ? HeaderHeight : LineHeight;
                    if (y + height < 0.0f || y > Graphics::CanvasHeight)
                        continue;
                    // Fade near the top and bottom edges.
                    const float edge = std::min(y + height * 0.5f, Graphics::CanvasHeight - y - height * 0.5f);
                    const float alpha = Clamp01(edge / 140.0f);
                    TextStyle style;
                    style.size = line.header ? 52.0f : 38.0f;
                    style.bold = line.header;
                    style.align = TextAlign::Center;
                    style.verticalCenter = true;
                    style.shadow = true;
                    style.color = line.color.WithAlpha(line.color.a * alpha);
                    ui.Text(line.text, D2D1::RectF(0, y, Graphics::CanvasWidth, y + height), style);
                }

                TextStyle hint;
                hint.size = 22.0f;
                hint.align = TextAlign::Right;
                hint.color = Color{ 0.8f, 0.85f, 0.95f, 0.6f };
                ui.Text(Localization::GetW("hint.credits"), D2D1::RectF(0, 1030, Graphics::CanvasWidth - 30, 1075), hint);
                gfx.End2D();
            }

        private:
            static constexpr float HeaderHeight = 80.0f;
            static constexpr float LineHeight = 54.0f;
            static constexpr float SectionGap = 70.0f;
            static constexpr float MinSpeed = 15.0f;
            static constexpr float MaxSpeed = 900.0f;

            struct Line
            {
                std::wstring text;
                Color color;
                float y;
                bool header;
            };

            void Finish()
            {
                if (m_done)
                    return;
                m_done = true;
                m_game.ShowTitle();
            }

            std::vector<CreditSection> m_sections;
            std::vector<Line> m_lines;
            float m_totalHeight = 0.0f;
            float m_scroll = 0.0f;
            float m_speed = 80.0f;
            bool m_done = false;
        };
    }

    std::unique_ptr<Screen> MakeCreditsScreen(Game& game)
    {
        return std::make_unique<CreditsScreen>(game);
    }
}
