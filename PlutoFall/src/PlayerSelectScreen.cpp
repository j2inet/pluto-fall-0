// PlayerSelectScreen.cpp - up to four players join (A / Enter), pick a craft with Left/Right,
// lock it in with fire and can back out with B. When every joined player has locked in, the
// opening dialog starts.
#include "Game.h"
#include "Localization.h"
#include "Screens.h"
#include "Util.h"

namespace pf
{
    namespace
    {
        constexpr int MaxPlayers = 4;
        constexpr float StartDelay = 1.2f;

        const Color SlotColors[MaxPlayers] = {
            { 0.35f, 0.85f, 1.0f, 1 },
            { 1.0f, 0.45f, 0.35f, 1 },
            { 0.45f, 1.0f, 0.45f, 1 },
            { 1.0f, 0.85f, 0.3f, 1 },
        };

        std::wstring DeviceLabel(int device)
        {
            if (device == Input::KeyboardDevice)
                return Localization::GetW("device.keyboard");
            std::wstring text = Localization::GetW("device.controller");
            if (auto pos = text.find(L"{0}"); pos != std::wstring::npos)
                text.replace(pos, 3, std::to_wstring(device));
            return text;
        }

        class PlayerSelectScreen final : public Screen
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
                Input& input = m_game.Controls();
                const auto& models = m_game.Data().Config().players;
                const int modelCount = static_cast<int>(models.size());

                for (int device = 0; device < Input::DeviceCount; ++device)
                {
                    Slot* slot = FindSlot(device);
                    if (!slot)
                    {
                        if (input.Pressed(device, Button::Join))
                        {
                            for (int i = 0; i < MaxPlayers; ++i)
                            {
                                if (m_slots[i].device < 0)
                                {
                                    m_slots[i] = Slot{ device, i % std::max(1, modelCount), false };
                                    m_game.PlayMenuSelect();
                                    input.Rumble(device, 0.3f, 0.3f, 0.15f);
                                    break;
                                }
                            }
                        }
                        continue;
                    }

                    if (input.Pressed(device, Button::Cancel))
                    {
                        if (slot->locked)
                            slot->locked = false;
                        else
                            slot->device = -1; // leave
                        m_game.PlayMenuMove();
                        continue;
                    }
                    if (slot->locked)
                        continue;
                    if (input.Pressed(device, Button::Left))
                    {
                        slot->model = (slot->model - 1 + modelCount) % modelCount;
                        m_game.PlayMenuMove();
                    }
                    else if (input.Pressed(device, Button::Right))
                    {
                        slot->model = (slot->model + 1) % modelCount;
                        m_game.PlayMenuMove();
                    }
                    else if (input.Pressed(device, Button::Primary))
                    {
                        slot->locked = true;
                        m_game.PlayMenuSelect();
                        input.Rumble(device, 0.5f, 0.5f, 0.2f);
                    }
                }

                const int joined = static_cast<int>(std::ranges::count_if(m_slots, [](const Slot& s) { return s.device >= 0; }));
                const bool allLocked = joined > 0 && std::ranges::all_of(m_slots, [](const Slot& s) { return s.device < 0 || s.locked; });

                if (joined == 0 && input.AnyPressed(Button::Menu))
                {
                    m_game.ShowTitle();
                    return;
                }

                if (allLocked)
                {
                    m_countdown += dt;
                    if (m_countdown >= StartDelay && !m_started)
                        Start();
                }
                else
                {
                    m_countdown = 0.0f;
                }
            }

            void Render() override
            {
                Graphics& gfx = m_game.Gfx();
                Ui& ui = m_game.UI();
                const auto& models = m_game.Data().Config().players;

                gfx.Begin2D();
                ui.Image(m_game.Data().Config().selectBackground,
                         D2D1::RectF(0, 0, Graphics::CanvasWidth, Graphics::CanvasHeight), 0.45f, true);

                TextStyle header;
                header.size = 64.0f;
                header.bold = true;
                header.align = TextAlign::Center;
                header.shadow = true;
                ui.Text(Localization::GetW("select.title"), D2D1::RectF(0, 60, Graphics::CanvasWidth, 150), header);

                for (int i = 0; i < MaxPlayers; ++i)
                {
                    const Slot& slot = m_slots[i];
                    const D2D1_RECT_F panel = PanelRect(i);
                    DrawPanel(ui, panel, slot.device >= 0 ? 0.75f : 0.45f);
                    const Color color = SlotColors[i];

                    TextStyle label;
                    label.size = 40.0f;
                    label.bold = true;
                    label.align = TextAlign::Center;
                    label.color = color;
                    label.shadow = true;
                    ui.Text(std::format(L"P{}", i + 1), D2D1::RectF(panel.left, panel.top + 16, panel.right, panel.top + 70), label);

                    TextStyle body;
                    body.size = 28.0f;
                    body.align = TextAlign::Center;
                    body.shadow = true;

                    if (slot.device < 0)
                    {
                        body.color = Color{ 1, 1, 1, 0.55f + 0.35f * std::sin(m_time * 4.0f + i) };
                        body.verticalCenter = true;
                        ui.Text(Localization::GetW("select.join"), D2D1::RectF(panel.left + 20, panel.top, panel.right - 20, panel.bottom), body);
                        continue;
                    }

                    body.color = Color{ 0.8f, 0.85f, 0.95f, 1 };
                    ui.Text(DeviceLabel(slot.device), D2D1::RectF(panel.left, panel.top + 72, panel.right, panel.top + 112), body);

                    const PlayerModelDef& def = models[static_cast<size_t>(slot.model)];
                    TextStyle name = body;
                    name.size = 34.0f;
                    name.bold = true;
                    name.color = Colors::White;
                    ui.Text(def.name.GetW(), D2D1::RectF(panel.left, panel.bottom - 230, panel.right, panel.bottom - 180), name);

                    if (!slot.locked)
                    {
                        TextStyle arrows = name;
                        arrows.size = 48.0f;
                        arrows.color = color;
                        arrows.verticalCenter = true;
                        const float cy = panel.top + 300.0f;
                        ui.Text(L"◀", D2D1::RectF(panel.left + 10, cy - 40, panel.left + 70, cy + 40), arrows);
                        ui.Text(L"▶", D2D1::RectF(panel.right - 70, cy - 40, panel.right - 10, cy + 40), arrows);
                    }

                    TextStyle status = body;
                    status.size = 30.0f;
                    status.bold = slot.locked;
                    status.color = slot.locked ? Colors::Green : Color{ 1, 1, 1, 0.7f + 0.3f * std::sin(m_time * 5.0f) };
                    ui.Text(Localization::GetW(slot.locked ? "select.ready" : "select.lock"),
                            D2D1::RectF(panel.left + 10, panel.bottom - 160, panel.right - 10, panel.bottom - 60), status);
                    TextStyle small = body;
                    small.size = 22.0f;
                    small.color = Color{ 0.75f, 0.8f, 0.9f, 0.8f };
                    ui.Text(Localization::GetW(slot.locked ? "select.unlock" : "select.leave"),
                            D2D1::RectF(panel.left + 10, panel.bottom - 60, panel.right - 10, panel.bottom - 16), small);
                }

                const bool anyJoined = std::ranges::any_of(m_slots, [](const Slot& s) { return s.device >= 0; });
                TextStyle hint;
                hint.size = 28.0f;
                hint.align = TextAlign::Center;
                hint.shadow = true;
                hint.color = Color{ 0.9f, 0.92f, 1.0f, 0.9f };
                std::wstring footer = Localization::GetW(anyJoined ? "select.waiting" : "select.back");
                if (m_countdown > 0.0f)
                    footer = Localization::GetW("select.starting");
                ui.Text(footer, D2D1::RectF(0, 990, Graphics::CanvasWidth, 1050), hint);
                gfx.End2D();

                // 3D previews of the selected craft.
                gfx.ClearDepth();
                Renderer3D& r = m_game.Renderer();
                r.BeginGameScene();
                for (int i = 0; i < MaxPlayers; ++i)
                {
                    const Slot& slot = m_slots[i];
                    if (slot.device < 0)
                        continue;
                    const PlayerModelDef& def = models[static_cast<size_t>(slot.model)];
                    auto model = m_game.Models().Load(def.model.model, def.model.textureFolder);
                    if (!model)
                        continue;
                    const D2D1_RECT_F panel = PanelRect(i);
                    const XMFLOAT3 pos = Renderer3D::CanvasToWorld((panel.left + panel.right) * 0.5f, panel.top + 300.0f);
                    const float spin = slot.locked ? m_time * 3.0f : m_time * 1.2f;
                    const XMMATRIX world = def.model.Transform() * XMMatrixRotationY(spin) * XMMatrixRotationX(-0.35f) *
                                           XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(pos.x, pos.y, -1.0f);
                    r.Draw(*model, world);
                }
            }

        private:
            struct Slot
            {
                int device = -1;
                int model = 0;
                bool locked = false;
            };

            static D2D1_RECT_F PanelRect(int index)
            {
                constexpr float Width = 400.0f, Gap = 40.0f, Top = 190.0f, Bottom = 950.0f;
                const float left = (Graphics::CanvasWidth - (Width * MaxPlayers + Gap * (MaxPlayers - 1))) * 0.5f +
                                   index * (Width + Gap);
                return D2D1::RectF(left, Top, left + Width, Bottom);
            }

            Slot* FindSlot(int device)
            {
                for (Slot& s : m_slots)
                    if (s.device == device)
                        return &s;
                return nullptr;
            }

            void Start()
            {
                m_started = true;
                const GameConfig& config = m_game.Data().Config();
                Session& session = m_game.CurrentSession();
                session.Reset();
                for (const Slot& s : m_slots)
                {
                    if (s.device < 0)
                        continue;
                    PlayerSlot player;
                    player.device = s.device;
                    player.modelIndex = s.model;
                    player.stats.lives = config.startLives;
                    player.stats.health = config.startHealth;
                    player.stats.maxHealth = config.startHealth;
                    player.stats.bombs = config.startBombs;
                    session.players.push_back(player);
                }
                m_game.ShowDialog(config.startDialog);
            }

            std::array<Slot, MaxPlayers> m_slots{};
            float m_time = 0.0f;
            float m_countdown = 0.0f;
            bool m_started = false;
        };
    }

    std::unique_ptr<Screen> MakePlayerSelectScreen(Game& game)
    {
        return std::make_unique<PlayerSelectScreen>(game);
    }
}
