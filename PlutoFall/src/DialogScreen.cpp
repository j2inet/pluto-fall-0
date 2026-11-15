// DialogScreen.cpp - story dialog with typewriter text, portraits, questions and branching.
#include "Game.h"
#include "Localization.h"
#include "Screens.h"
#include "Util.h"

namespace pf
{
    namespace
    {
        class DialogScreen final : public Screen
        {
        public:
            DialogScreen(Game& game, std::string path) : Screen(game), m_path(std::move(path)) {}

            void Enter() override
            {
                auto def = DialogDef::Load(m_path);
                if (!def)
                {
                    Log("[PlutoFall] Dialog '{}' could not be loaded", m_path);
                    m_failed = true;
                    return;
                }
                m_def = std::move(*def);
                if (!m_def.music.empty())
                    m_game.Sound().PlayMusic(m_def.music);
                m_section = 0;
                m_entry = -1;
                Advance();
            }

            void Exit() override
            {
                StopTyping();
            }

            void Update(float dt) override
            {
                m_time += dt;
                if (m_failed)
                {
                    // Nothing to show; continue with the default flow.
                    m_failed = false;
                    Finish();
                    return;
                }
                Input& input = m_game.Controls();

                switch (m_phase)
                {
                case Phase::Typing:
                    m_reveal += m_def.charactersPerSecond * dt;
                    if (input.AnyPressed(Button::Primary))
                        m_reveal = static_cast<float>(m_text.size());
                    if (m_reveal >= static_cast<float>(m_text.size()))
                    {
                        m_reveal = static_cast<float>(m_text.size());
                        StopTyping();
                        m_phase = CurrentEntry() && !CurrentEntry()->responses.empty() ? Phase::Choosing : Phase::Waiting;
                        m_choices.selected = 0;
                    }
                    break;

                case Phase::Waiting:
                    if (input.AnyConfirm())
                    {
                        m_game.PlayMenuMove();
                        Advance();
                    }
                    break;

                case Phase::Choosing:
                {
                    const DialogEntry* entry = CurrentEntry();
                    if (!entry)
                    {
                        Advance();
                        break;
                    }
                    m_choices.Navigate(m_game, static_cast<int>(entry->responses.size()), dt);
                    if (input.AnyConfirm())
                    {
                        m_game.PlayMenuSelect();
                        const DialogResponse& response = entry->responses[static_cast<size_t>(m_choices.selected)];
                        if (!entry->key.empty())
                            m_answers[entry->key] = response.id;
                        if (!response.gotoSection.empty())
                        {
                            const int target = m_def.FindSection(response.gotoSection);
                            if (target >= 0)
                            {
                                m_section = target;
                                m_entry = -1;
                                Advance();
                                break;
                            }
                            Log("[PlutoFall] Dialog goto target '{}' not found", response.gotoSection);
                        }
                        Advance();
                    }
                    break;
                }

                case Phase::Finished:
                    break;
                }
            }

            void Render() override
            {
                Graphics& gfx = m_game.Gfx();
                Ui& ui = m_game.UI();
                gfx.Begin2D();
                ui.FillScreen(Colors::Black);

                const DialogSection* section = CurrentSection();
                if (section && !section->background.empty())
                    ui.Image(section->background, D2D1::RectF(0, 0, Graphics::CanvasWidth, Graphics::CanvasHeight), 1.0f, true);

                const DialogEntry* entry = CurrentEntry();
                if (!entry || m_phase == Phase::Finished)
                {
                    gfx.End2D();
                    return;
                }

                const D2D1_RECT_F box = D2D1::RectF(60, 730, 1860, 1040);

                // Speaker portrait.
                if (!entry->portrait.empty())
                {
                    const D2D1_RECT_F frame = D2D1::RectF(90, 330, 450, 690);
                    ui.FillRect(frame, m_def.boxColor);
                    ui.Image(entry->portrait, frame, 1.0f, true);
                    ui.DrawRect(frame, m_def.borderColor, 4.0f);
                }

                // Speaker name plate.
                const std::wstring speaker = entry->speaker.GetW();
                if (!speaker.empty())
                {
                    TextStyle nameStyle;
                    nameStyle.size = 34.0f;
                    nameStyle.bold = true;
                    nameStyle.verticalCenter = true;
                    nameStyle.wrap = false;
                    const float width = std::max(260.0f, ui.Measure(speaker, 1200.0f, nameStyle).width + 60.0f);
                    const D2D1_RECT_F plate = D2D1::RectF(90, 676, 90 + width, 730);
                    ui.FillRect(plate, m_def.boxColor);
                    ui.DrawRect(plate, m_def.borderColor, 3.0f);
                    nameStyle.color = Colors::Gold;
                    ui.Text(speaker, D2D1::RectF(plate.left + 30, plate.top, plate.right - 10, plate.bottom), nameStyle);
                }

                // Text box: semi-transparent solid background with a solid border.
                ui.FillRect(box, m_def.boxColor);
                ui.DrawRect(box, m_def.borderColor, 4.0f);

                size_t visible = std::min(m_text.size(), static_cast<size_t>(m_reveal));
                if (visible > 0 && visible < m_text.size() && IS_HIGH_SURROGATE(m_text[visible - 1]))
                    ++visible; // never split a surrogate pair
                TextStyle textStyle;
                textStyle.size = 40.0f;
                textStyle.color = m_def.textColor;
                textStyle.shadow = true;
                ui.Text(std::wstring_view(m_text).substr(0, visible), D2D1::RectF(box.left + 40, box.top + 30, box.right - 40, box.bottom - 30),
                        textStyle);

                if (m_phase == Phase::Waiting && std::fmod(m_time, 1.0f) < 0.6f)
                {
                    TextStyle arrow;
                    arrow.size = 32.0f;
                    arrow.color = m_def.borderColor;
                    arrow.align = TextAlign::Right;
                    ui.Text(L"▼", D2D1::RectF(box.right - 100, box.bottom - 56, box.right - 30, box.bottom - 10), arrow);
                }

                if (m_phase == Phase::Choosing)
                {
                    const size_t count = entry->responses.size();
                    const float itemHeight = 70.0f;
                    const float height = itemHeight * static_cast<float>(count) + 40.0f;
                    const D2D1_RECT_F panel = D2D1::RectF(1000, box.top - 20 - height, 1840, box.top - 20);
                    ui.FillRect(panel, m_def.boxColor);
                    ui.DrawRect(panel, m_def.borderColor, 3.0f);
                    for (size_t i = 0; i < count; ++i)
                    {
                        const float y = panel.top + 20.0f + itemHeight * static_cast<float>(i);
                        const bool active = static_cast<int>(i) == m_choices.selected;
                        if (active)
                            ui.FillRect(D2D1::RectF(panel.left + 12, y + 4, panel.right - 12, y + itemHeight - 4),
                                        Color{ m_def.borderColor.r, m_def.borderColor.g, m_def.borderColor.b, 0.35f });
                        TextStyle option;
                        option.size = 34.0f;
                        option.bold = active;
                        option.verticalCenter = true;
                        option.color = active ? Colors::White : Color{ 0.8f, 0.85f, 0.92f, 1.0f };
                        const std::wstring text = (active ? L"▶ " : L"   ") + entry->responses[i].text.GetW();
                        ui.Text(text, D2D1::RectF(panel.left + 30, y, panel.right - 20, y + itemHeight), option);
                    }
                }
                gfx.End2D();
            }

        private:
            enum class Phase
            {
                Typing,
                Waiting,
                Choosing,
                Finished
            };

            const DialogSection* CurrentSection() const
            {
                if (m_section < 0 || m_section >= static_cast<int>(m_def.sections.size()))
                    return nullptr;
                return &m_def.sections[static_cast<size_t>(m_section)];
            }

            const DialogEntry* CurrentEntry() const
            {
                const DialogSection* section = CurrentSection();
                if (!section || m_entry < 0 || m_entry >= static_cast<int>(section->entries.size()))
                    return nullptr;
                return &section->entries[static_cast<size_t>(m_entry)];
            }

            // Moves to the next entry, then the next section; ends the dialog when done.
            void Advance()
            {
                StopTyping();
                for (int guard = 0; guard < 10000; ++guard)
                {
                    const DialogSection* section = CurrentSection();
                    if (!section)
                    {
                        Finish();
                        return;
                    }
                    ++m_entry;
                    if (m_entry < static_cast<int>(section->entries.size()))
                    {
                        BeginEntry();
                        return;
                    }
                    // End of this section.
                    if (section->terminal)
                    {
                        Finish();
                        return;
                    }
                    ++m_section;
                    m_entry = -1;
                }
                Finish();
            }

            void BeginEntry()
            {
                m_text = CurrentEntry()->text.GetW();
                m_reveal = 0.0f;
                m_phase = Phase::Typing;
                if (!m_def.typeSound.empty())
                    m_typeLoop = m_game.Sound().StartLoop(m_def.typeSound, Audio::Bus::Effects, 0.6f);
            }

            void StopTyping()
            {
                if (m_typeLoop)
                {
                    m_game.Sound().StopLoop(m_typeLoop);
                    m_typeLoop = 0;
                }
            }

            void Finish()
            {
                if (m_phase == Phase::Finished && m_finishedOnce)
                    return;
                m_finishedOnce = true;
                m_phase = Phase::Finished;
                StopTyping();

                // Merge this dialog's answers into the session and pick the next level.
                Session& session = m_game.CurrentSession();
                for (const auto& [key, value] : m_answers)
                    session.choices[key] = value;
                const std::string level = SelectNextLevel(session.choices, m_def.nextLevels);
                if (level.empty())
                    m_game.ShowTitle();
                else
                    m_game.ShowLevel(level);
            }

            std::string m_path;
            DialogDef m_def;
            bool m_failed = false;
            bool m_finishedOnce = false;
            int m_section = 0;
            int m_entry = -1;
            Phase m_phase = Phase::Typing;
            std::wstring m_text;
            float m_reveal = 0.0f;
            float m_time = 0.0f;
            int m_typeLoop = 0;
            MenuList m_choices;
            std::map<std::string, std::string> m_answers;
        };
    }

    std::unique_ptr<Screen> MakeDialogScreen(Game& game, const std::string& dialogPath)
    {
        return std::make_unique<DialogScreen>(game, dialogPath);
    }
}
