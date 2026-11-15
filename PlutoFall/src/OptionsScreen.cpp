// OptionsScreen.cpp - sound effect volume, music volume, language and display mode.
#include "Game.h"
#include "Localization.h"
#include "Screens.h"
#include "Util.h"

namespace pf
{
    namespace
    {
        class OptionsScreen final : public Screen
        {
        public:
            using Screen::Screen;

            void Update(float dt) override
            {
                m_time += dt;
                Input& input = m_game.Controls();
                m_menu.Navigate(m_game, ItemCount, dt);

                int horizontal = 0;
                if (input.AnyPressed(Button::Left))
                    horizontal = -1;
                else if (input.AnyPressed(Button::Right))
                    horizontal = 1;

                // Holding left/right repeats for the sliders.
                if (input.AnyDown(Button::Left) || input.AnyDown(Button::Right))
                {
                    m_hold += dt;
                    if (m_hold > 0.4f && horizontal == 0)
                    {
                        m_repeat -= dt;
                        if (m_repeat <= 0.0f)
                        {
                            horizontal = input.AnyDown(Button::Left) ? -1 : 1;
                            m_repeat = 0.08f;
                        }
                    }
                }
                else
                {
                    m_hold = 0.0f;
                    m_repeat = 0.0f;
                }

                Settings& s = m_game.Options();
                if (horizontal != 0)
                    Adjust(s, horizontal);

                if (input.AnyBack())
                {
                    Leave();
                    return;
                }
                if (input.AnyConfirm())
                {
                    if (m_menu.selected == ItemBack)
                        Leave();
                    else
                        Adjust(s, 1);
                }
            }

            void Render() override
            {
                Graphics& gfx = m_game.Gfx();
                Ui& ui = m_game.UI();
                const Settings& s = m_game.Options();
                gfx.Begin2D();
                ui.Image(m_game.Data().Config().titleImage, D2D1::RectF(0, 0, Graphics::CanvasWidth, Graphics::CanvasHeight),
                         0.35f, true);

                const D2D1_RECT_F panel = D2D1::RectF(360, 140, 1560, 940);
                DrawPanel(ui, panel);

                TextStyle header;
                header.size = 64.0f;
                header.bold = true;
                header.align = TextAlign::Center;
                header.shadow = true;
                ui.Text(Localization::GetW("options.title"), D2D1::RectF(360, 170, 1560, 260), header);

                const std::wstring labels[ItemCount] = {
                    Localization::GetW("options.sfx"), Localization::GetW("options.music"),
                    Localization::GetW("options.language"), Localization::GetW("options.fullscreen"),
                    Localization::GetW("options.back"),
                };

                const auto& languages = Localization::Languages();
                std::wstring languageName = Widen(Localization::Current());
                for (const auto& lang : languages)
                    if (lang.code == Localization::Current())
                        languageName = Widen(lang.name);

                for (int i = 0; i < ItemCount; ++i)
                {
                    const float y = 320.0f + i * 110.0f;
                    const bool active = m_menu.selected == i;
                    if (active)
                    {
                        const float pulse = 0.55f + 0.25f * std::sin(m_time * 5.0f);
                        ui.FillRoundRect(D2D1::RectF(420, y - 8, 1500, y + 82), 12, Color{ 0.15f, 0.45f, 0.8f, pulse * 0.5f });
                    }
                    TextStyle label;
                    label.size = 40.0f;
                    label.bold = active;
                    label.verticalCenter = true;
                    label.align = i == ItemBack ? TextAlign::Center : TextAlign::Left;
                    const D2D1_RECT_F labelRect =
                        i == ItemBack ? D2D1::RectF(420, y, 1500, y + 74) : D2D1::RectF(460, y, 900, y + 74);
                    ui.Text(labels[i], labelRect, label);

                    TextStyle value = label;
                    value.align = TextAlign::Center;
                    const D2D1_RECT_F valueRect = D2D1::RectF(920, y, 1460, y + 74);
                    switch (i)
                    {
                    case ItemSfx:
                    case ItemMusic:
                    {
                        const float v = i == ItemSfx ? s.sfxVolume : s.musicVolume;
                        ui.Meter(D2D1::RectF(940, y + 22, 1300, y + 52), v, Color{ 0.3f, 0.85f, 1.0f, 1 },
                                 Color{ 0.1f, 0.12f, 0.2f, 0.9f });
                        ui.Text(std::format(L"{}%", static_cast<int>(std::round(v * 100.0f))),
                                D2D1::RectF(1310, y, 1460, y + 74), value);
                        break;
                    }
                    case ItemLanguage:
                        ui.Text(L"◀  " + languageName + L"  ▶", valueRect, value);
                        break;
                    case ItemFullscreen:
                        ui.Text(Localization::GetW(s.fullscreen ? "options.on" : "options.off"), valueRect, value);
                        break;
                    default:
                        break;
                    }
                }

                TextStyle hint;
                hint.size = 24.0f;
                hint.align = TextAlign::Center;
                hint.color = Color{ 0.8f, 0.85f, 0.95f, 0.8f };
                ui.Text(Localization::GetW("hint.options"), D2D1::RectF(0, 1000, Graphics::CanvasWidth, 1060), hint);
                gfx.End2D();
            }

        private:
            enum
            {
                ItemSfx,
                ItemMusic,
                ItemLanguage,
                ItemFullscreen,
                ItemBack,
                ItemCount
            };

            void Adjust(Settings& s, int direction)
            {
                constexpr float Step = 0.05f;
                switch (m_menu.selected)
                {
                case ItemSfx:
                    s.sfxVolume = Clamp01(std::round((s.sfxVolume + direction * Step) * 20.0f) / 20.0f);
                    m_game.ApplySettings();
                    m_game.Sound().PlaySfx(m_game.Data().Config().sounds.shoot);
                    break;
                case ItemMusic:
                    s.musicVolume = Clamp01(std::round((s.musicVolume + direction * Step) * 20.0f) / 20.0f);
                    m_game.ApplySettings();
                    break;
                case ItemLanguage:
                {
                    const auto& languages = Localization::Languages();
                    if (languages.empty())
                        break;
                    int index = 0;
                    for (size_t i = 0; i < languages.size(); ++i)
                        if (languages[i].code == Localization::Current())
                            index = static_cast<int>(i);
                    const int count = static_cast<int>(languages.size());
                    index = (index + direction + count) % count;
                    Localization::SetCurrent(languages[static_cast<size_t>(index)].code);
                    s.language = Localization::Current();
                    m_game.ApplySettings();
                    m_game.PlayMenuMove();
                    break;
                }
                case ItemFullscreen:
                    m_game.ToggleFullscreen();
                    m_game.PlayMenuMove();
                    break;
                default:
                    break;
                }
            }

            void Leave()
            {
                m_game.Options().Save();
                m_game.PlayMenuSelect();
                m_game.ShowTitle();
            }

            MenuList m_menu;
            float m_time = 0.0f;
            float m_hold = 0.0f;
            float m_repeat = 0.0f;
        };
    }

    std::unique_ptr<Screen> MakeOptionsScreen(Game& game)
    {
        return std::make_unique<OptionsScreen>(game);
    }
}
