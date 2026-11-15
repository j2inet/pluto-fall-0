// GameplayScreen.cpp - the scrolling shooter itself.
//
// Playfield coordinates: the visible area is centred on (0, 0) on the z = 0 plane. In landscape
// mode it spans x in [-16, 16] and y in [-9, 9]; in portrait mode the field is narrowed to
// x in [-8, 8] with HUD panels at the sides. Players, enemies, items and projectiles live in these
// screen-relative coordinates. Background objects use level (world) coordinates and are drawn
// relative to the scrolling camera, so objects with larger z scroll past with natural parallax.
#include "FireFunctions.h"
#include "Game.h"
#include "Localization.h"
#include "MotionFunctions.h"
#include "Screens.h"
#include "Util.h"

namespace pf
{
    namespace
    {
        constexpr float IntroDuration = 1.6f;
        constexpr float FadeOutDuration = 1.5f;
        constexpr float GameOverDuration = 4.0f;
        constexpr float RespawnDelay = 1.5f;
        constexpr float RespawnInvulnerability = 2.5f;
        constexpr float HitInvulnerability = 0.6f;
        constexpr float CullMargin = 4.0f;

        const Color PlayerColors[4] = {
            { 0.35f, 0.85f, 1.0f, 1 },
            { 1.0f, 0.45f, 0.35f, 1 },
            { 0.45f, 1.0f, 0.45f, 1 },
            { 1.0f, 0.85f, 0.3f, 1 },
        };

        float Dist2(XMFLOAT3 a, XMFLOAT3 b)
        {
            const float dx = a.x - b.x, dy = a.y - b.y;
            return dx * dx + dy * dy;
        }

        bool Overlaps(XMFLOAT3 a, float ra, XMFLOAT3 b, float rb)
        {
            const float r = ra + rb;
            return Dist2(a, b) < r * r;
        }

        class GameplayScreen final : public Screen
        {
        public:
            GameplayScreen(Game& game, std::string levelPath) : Screen(game), m_levelPath(std::move(levelPath)) {}

            void Enter() override
            {
                auto level = LevelDef::Load(m_levelPath);
                if (!level)
                {
                    Log("[PlutoFall] Level '{}' could not be loaded", m_levelPath);
                    m_failed = true;
                    return;
                }
                m_level = std::move(*level);
                m_orientation = m_level.orientation;
                m_axes = Axes::For(m_orientation);
                m_exit = { -m_axes.forward.x, -m_axes.forward.y };
                m_half = m_orientation == Orientation::Landscape ? XMFLOAT2{ 16.0f, 9.0f } : XMFLOAT2{ 8.0f, 9.0f };

                if (!m_level.music.empty())
                    m_game.Sound().PlayMusic(m_level.music);

                const GameConfig& config = m_game.Data().Config();
                ModelCache& models = m_game.Models();
                m_bulletModel = ProjectileModel(config.playerBullet);
                m_bombModel = ProjectileModel(config.playerBomb);
                m_particleModel = models.Box({ 1, 1, 1 }, Colors::White, true);

                // Background decoration.
                for (const BackgroundDef& bg : m_level.background)
                {
                    Backdrop b;
                    b.model = models.Load(bg.model.model, bg.model.textureFolder);
                    b.transform = bg.model.transform;
                    b.position = bg.position;
                    b.spin = bg.spin;
                    if (!bg.motion.Empty())
                    {
                        b.motion = MotionRegistry::Create(bg.motion.name, bg.motion.params);
                        if (b.motion)
                            b.motion->Begin(bg.position, m_level.scrollSpeed);
                    }
                    m_backdrops.push_back(std::move(b));
                }

                // Players.
                Session& session = m_game.CurrentSession();
                const size_t count = session.players.size();
                for (size_t i = 0; i < count; ++i)
                {
                    PlayerSlot& slot = session.players[i];
                    const auto& defs = config.players;
                    Ship ship;
                    ship.slot = static_cast<int>(i);
                    ship.device = slot.device;
                    ship.def = &defs[static_cast<size_t>(std::clamp(slot.modelIndex, 0, static_cast<int>(defs.size()) - 1))];
                    ship.model = models.Load(ship.def->model.model, ship.def->model.textureFolder);
                    ship.color = PlayerColors[i % 4];
                    const float across = (static_cast<float>(i) - (count - 1) * 0.5f) * 3.0f;
                    const float along = AlongHalf() - 4.0f;
                    ship.home = { m_exit.x * -along + m_axes.across.x * across, m_exit.y * -along + m_axes.across.y * across, 0 };
                    ship.introStart = { ship.home.x - m_exit.x * 10.0f, ship.home.y - m_exit.y * 10.0f, 0 };
                    ship.pos = ship.introStart;
                    ship.alive = slot.stats.lives > 0;
                    ship.invulnerable = IntroDuration + 0.5f;
                    if (slot.stats.health <= 0)
                        slot.stats.health = slot.stats.maxHealth;
                    m_ships.push_back(std::move(ship));
                }
                m_phase = Phase::Intro;
            }

            void Update(float dt) override
            {
                if (m_failed)
                {
                    m_failed = false;
                    m_game.ShowTitle();
                    return;
                }
                Input& input = m_game.Controls();

                if (m_paused)
                {
                    UpdatePause(dt);
                    return;
                }
                if ((m_phase == Phase::Intro || m_phase == Phase::Playing) && input.AnyPressed(Button::Menu))
                {
                    m_paused = true;
                    m_pauseMenu.selected = 0;
                    m_game.PlayMenuMove();
                    return;
                }

                m_time += dt;
                m_phaseTime += dt;
                m_bannerTime = std::max(0.0f, m_bannerTime - dt);

                switch (m_phase)
                {
                case Phase::Intro:
                    if (m_phaseTime >= IntroDuration)
                        SetPhase(Phase::Playing);
                    break;
                case Phase::Playing:
                    if (m_scroll >= m_level.length)
                    {
                        // End of the play area: bring on any remaining spawns (e.g. the boss).
                        while (m_nextSpawn < m_level.spawns.size())
                            Spawn(m_level.spawns[m_nextSpawn++]);
                        const bool bossAlive = std::ranges::any_of(m_enemies, [](const Enemy& e) { return e.boss && !e.dead; });
                        if (!bossAlive)
                            SetPhase(Phase::Exiting);
                    }
                    break;
                case Phase::FadeOut:
                    m_fade = Clamp01(m_phaseTime / FadeOutDuration);
                    if (m_phaseTime >= FadeOutDuration)
                        CompleteLevel();
                    break;
                case Phase::GameOver:
                    m_fade = Clamp01((m_phaseTime - GameOverDuration + 1.0f));
                    if (m_phaseTime >= GameOverDuration || (m_phaseTime > 1.5f && input.AnyConfirm()))
                    {
                        if (!m_leaving)
                        {
                            m_leaving = true;
                            m_game.ShowTitle();
                        }
                    }
                    break;
                default:
                    break;
                }

                // Scroll and activate spawns.
                if (m_phase == Phase::Intro || m_phase == Phase::Playing)
                {
                    m_scroll = std::min(m_level.length, m_scroll + m_level.scrollSpeed * dt);
                    while (m_nextSpawn < m_level.spawns.size() &&
                           m_level.spawns[m_nextSpawn].Activation(m_orientation) <= m_scroll)
                    {
                        Spawn(m_level.spawns[m_nextSpawn++]);
                    }
                }

                UpdateShips(dt);
                UpdateEnemies(dt);
                UpdateItems(dt);
                UpdateShots(dt);
                if (m_phase == Phase::Playing || m_phase == Phase::Intro)
                    Collide();
                UpdateParticles(dt);

                // Remove the dead.
                for (Enemy& e : m_enemies)
                    if (e.life <= 0.0f && !e.dead)
                        KillEnemy(e);
                std::erase_if(m_enemies, [](const Enemy& e) { return e.dead; });
                std::erase_if(m_items, [](const Item& i) { return i.dead; });
                std::erase_if(m_shots, [](const Shot& s) { return s.dead; });

                if (m_phase == Phase::Exiting)
                {
                    const bool allGone = std::ranges::all_of(m_ships, [&](const Ship& s)
                    {
                        return !s.alive || Along(s.pos) > AlongHalf() + 2.5f;
                    });
                    if (allGone)
                        SetPhase(Phase::FadeOut);
                }
            }

            void Render() override
            {
                if (m_failed)
                    return;
                Graphics& gfx = m_game.Gfx();
                Ui& ui = m_game.UI();
                Renderer3D& r = m_game.Renderer();

                // ---- background colour/image
                gfx.Begin2D();
                ui.FillScreen(m_level.backgroundColor);
                if (!m_level.backgroundImage.empty())
                    DrawScrollingImage(ui);
                gfx.End2D();

                // ---- 3D
                gfx.ClearDepth();
                r.BeginGameScene();
                const XMFLOAT2 camera{ m_exit.x * m_scroll, m_exit.y * m_scroll };
                const float cameraDistance = Renderer3D::CameraDistance();
                for (const Backdrop& b : m_backdrops)
                {
                    XMFLOAT3 world = b.motion ? b.motion->Evaluate(m_time, m_orientation) : b.position;
                    const XMFLOAT3 view{ world.x - camera.x, world.y - camera.y, world.z };
                    const float depthScale = std::max(0.1f, (cameraDistance + view.z) / cameraDistance);
                    if (std::fabs(view.x) > (Renderer3D::ViewHalfWidth + 8.0f) * depthScale ||
                        std::fabs(view.y) > (Renderer3D::ViewHalfHeight + 8.0f) * depthScale)
                        continue;
                    const XMMATRIX spin = XMMatrixRotationRollPitchYaw(XMConvertToRadians(b.spin.x * m_time),
                                                                       XMConvertToRadians(b.spin.y * m_time),
                                                                       XMConvertToRadians(b.spin.z * m_time));
                    r.Draw(*b.model, XMLoadFloat4x4(&b.transform) * spin * XMMatrixTranslation(view.x, view.y, view.z));
                }

                for (const Item& item : m_items)
                {
                    const XMMATRIX world = item.def->model.Transform() * XMMatrixRotationY(XMConvertToRadians(item.def->spin) * item.age) *
                                           XMMatrixRotationX(0.35f) * XMMatrixTranslation(item.pos.x, item.pos.y, item.pos.z);
                    const float pulse = 0.5f + 0.5f * std::sin(item.age * 6.0f);
                    r.Draw(*item.model, world, { 1, 1, 1, 1 },
                           { item.def->glow.r * pulse, item.def->glow.g * pulse, item.def->glow.b * pulse });
                }

                for (const Enemy& e : m_enemies)
                {
                    const XMMATRIX world = e.def->model.Transform() * Facing(false) * XMMatrixTranslation(e.pos.x, e.pos.y, e.pos.z);
                    const float f = e.flash > 0.0f ? 0.9f : 0.0f;
                    r.Draw(*e.model, world, { 1, 1, 1, 1 }, { f, f * 0.9f, f * 0.8f });
                }

                for (const Ship& s : m_ships)
                {
                    if (!s.alive)
                        continue;
                    if (s.invulnerable > 0.0f && m_phase == Phase::Playing && std::fmod(s.invulnerable, 0.16f) < 0.07f)
                        continue; // blink while invulnerable
                    const XMMATRIX world = s.def->model.Transform() * XMMatrixRotationX(s.bank) * Facing(true) *
                                           XMMatrixTranslation(s.pos.x, s.pos.y, s.pos.z);
                    const float f = s.flash > 0.0f ? 1.0f : 0.0f;
                    r.Draw(*s.model, world, { 1, 1, 1, 1 }, { f, f * 0.3f, f * 0.2f });
                }

                for (const Shot& shot : m_shots)
                {
                    const float angle = std::atan2(shot.vel.y, shot.vel.x);
                    const XMMATRIX spin = shot.bomb ? XMMatrixRotationRollPitchYaw(shot.age * 7.0f, shot.age * 5.0f, 0.0f)
                                                    : XMMatrixRotationZ(angle);
                    r.Draw(*shot.model, XMLoadFloat4x4(&shot.transform) * spin * XMMatrixTranslation(shot.pos.x, shot.pos.y, shot.pos.z));
                }

                for (const Particle& p : m_particles)
                {
                    const float t = Clamp01(p.life / p.maxLife);
                    const float size = p.size * (0.4f + 0.6f * t);
                    const XMMATRIX world = XMMatrixScaling(size, size, size) * XMMatrixRotationZ(p.life * 9.0f) *
                                           XMMatrixTranslation(p.pos.x, p.pos.y, p.pos.z);
                    r.Draw(*m_particleModel, world, { p.color.r, p.color.g, p.color.b, t * 0.9f });
                }

                // ---- 2D overlay
                gfx.Begin2D();
                if (m_orientation == Orientation::Portrait)
                    DrawPortraitFrame(ui);
                DrawHud(ui);
                DrawBanners(ui);
                if (m_paused)
                    DrawPause(ui);
                if (m_fade > 0.0f)
                    ui.FillScreen(Color{ 0, 0, 0, m_fade });
                if (m_phase == Phase::GameOver)
                {
                    TextStyle style;
                    style.size = 120.0f;
                    style.bold = true;
                    style.align = TextAlign::Center;
                    style.verticalCenter = true;
                    style.shadow = true;
                    style.color = Color{ 1.0f, 0.3f, 0.25f, Clamp01(m_phaseTime * 2.0f) };
                    ui.Text(Localization::GetW("game.over"), D2D1::RectF(0, 0, Graphics::CanvasWidth, Graphics::CanvasHeight), style);
                }
                gfx.End2D();
            }

        private:
            enum class Phase
            {
                Intro,
                Playing,
                Exiting,
                FadeOut,
                GameOver
            };

            struct Ship
            {
                int slot = 0;
                int device = 0;
                const PlayerModelDef* def = nullptr;
                std::shared_ptr<Model> model;
                Color color;
                XMFLOAT3 pos{ 0, 0, 0 };
                XMFLOAT3 home{ 0, 0, 0 };
                XMFLOAT3 introStart{ 0, 0, 0 };
                float exitSpeed = 0.0f;
                float invulnerable = 0.0f;
                float fireCooldown = 0.0f;
                float respawn = 0.0f;
                float flash = 0.0f;
                float bank = 0.0f;
                bool alive = true;
            };

            struct Enemy
            {
                const EnemyDef* def = nullptr;
                std::shared_ptr<Model> model;
                std::shared_ptr<Model> projectileModel;
                std::unique_ptr<MotionFunction> motion;
                std::unique_ptr<FireFunction> fire;
                XMFLOAT3 pos{ 0, 0, 0 };
                float life = 1.0f;
                float maxLife = 1.0f;
                float age = 0.0f;
                float flash = 0.0f;
                int lastHitBy = -1;
                bool boss = false;
                bool entered = false;
                bool dead = false;
            };

            struct Item
            {
                const ItemDef* def = nullptr;
                std::shared_ptr<Model> model;
                std::unique_ptr<MotionFunction> motion;
                XMFLOAT3 pos{ 0, 0, 0 };
                float age = 0.0f;
                bool entered = false;
                bool dead = false;
            };

            struct Shot
            {
                std::shared_ptr<Model> model;
                XMFLOAT4X4 transform;
                XMFLOAT3 pos{ 0, 0, 0 };
                XMFLOAT3 vel{ 0, 0, 0 };
                float damage = 1.0f;
                float life = 1.0f;
                float radius = 0.2f;
                float age = 0.0f;
                int owner = -1; // player slot, -1 for enemy shots
                bool fromPlayer = false;
                bool bomb = false;
                bool dead = false;
            };

            struct Particle
            {
                XMFLOAT3 pos;
                XMFLOAT3 vel;
                float life;
                float maxLife;
                float size;
                Color color;
            };

            struct Backdrop
            {
                std::shared_ptr<Model> model;
                XMFLOAT4X4 transform;
                XMFLOAT3 position;
                XMFLOAT3 spin;
                std::unique_ptr<MotionFunction> motion;
            };

            // ------------------------------------------------------------ geometry helpers

            // Half extent of the field along the scroll axis.
            float AlongHalf() const { return m_orientation == Orientation::Landscape ? m_half.x : m_half.y; }
            // Position along the exit direction (where players fly toward).
            float Along(XMFLOAT3 p) const { return p.x * m_exit.x + p.y * m_exit.y; }

            bool InField(XMFLOAT3 p, float margin) const
            {
                return std::fabs(p.x) <= m_half.x + margin && std::fabs(p.y) <= m_half.y + margin;
            }

            // Rotation that points a model (authored nose +X, top +Y) along the direction of travel.
            // Landscape is viewed from the side; portrait is viewed from above.
            XMMATRIX Facing(bool player) const
            {
                // Landscape: side view, tilted toward the camera so the top of the craft is visible.
                constexpr float Tilt = -0.55f;
                if (m_orientation == Orientation::Landscape)
                    return player ? XMMatrixRotationX(Tilt) : XMMatrixRotationY(Pi) * XMMatrixRotationX(Tilt);
                return XMMatrixRotationX(-Pi * 0.5f) * XMMatrixRotationZ(player ? Pi * 0.5f : -Pi * 0.5f);
            }

            std::shared_ptr<Model> ProjectileModel(const ProjectileSpec& spec)
            {
                if (!spec.model.model.empty())
                    return m_game.Models().Load(spec.model.model, spec.model.textureFolder);
                const XMFLOAT3 glow{ spec.color.r * 0.6f, spec.color.g * 0.6f, spec.color.b * 0.6f };
                return m_game.Models().Box(spec.size, spec.color, true, glow);
            }

            std::vector<XMFLOAT3> LivingPlayerPositions() const
            {
                std::vector<XMFLOAT3> positions;
                for (const Ship& s : m_ships)
                    if (s.alive)
                        positions.push_back(s.pos);
                return positions;
            }

            PlayerStats& Stats(const Ship& ship)
            {
                return m_game.CurrentSession().players[static_cast<size_t>(ship.slot)].stats;
            }

            void SetPhase(Phase phase)
            {
                if (m_phase == phase)
                    return;
                m_phase = phase;
                m_phaseTime = 0.0f;
                if (phase == Phase::Exiting)
                {
                    // Clear the skies so nothing can hit the players on the way out.
                    for (Shot& s : m_shots)
                        if (!s.fromPlayer)
                            s.dead = true;
                    for (Ship& s : m_ships)
                    {
                        s.invulnerable = 100.0f;
                        s.exitSpeed = 2.0f;
                    }
                }
                else if (phase == Phase::GameOver)
                {
                    m_game.Sound().PlaySfx(m_game.Data().Config().sounds.gameOver);
                }
            }

            // ------------------------------------------------------------ spawning

            void Spawn(const SpawnDef& spawn)
            {
                if (spawn.isItem)
                {
                    const ItemDef* def = m_game.Data().Item(spawn.className);
                    if (!def)
                        def = &FallbackItem(spawn.className);
                    SpawnItem(*def, spawn.position, spawn.motion);
                    return;
                }

                const EnemyDef* def = m_game.Data().Enemy(spawn.className);
                if (!def)
                    def = &FallbackEnemy(spawn.className);

                Enemy e;
                e.def = def;
                e.model = m_game.Models().Load(def->model.model, def->model.textureFolder);
                e.projectileModel = ProjectileModel(def->projectile);
                const FunctionSpec& motion = spawn.motion.Empty() ? def->motion : spawn.motion;
                e.motion = MotionRegistry::Create(motion.name, motion.params);
                if (e.motion)
                    e.motion->Begin(spawn.position, m_level.scrollSpeed);
                const FunctionSpec& fire = spawn.fire.Empty() ? def->fire : spawn.fire;
                e.fire = FireRegistry::Create(fire.name, fire.params);
                e.life = e.maxLife = spawn.life.value_or(def->life);
                e.boss = spawn.boss.value_or(def->boss);
                e.pos = spawn.position;
                if (e.boss)
                {
                    m_bannerTime = 3.0f;
                    m_bannerText = Localization::GetW("game.warning");
                }
                m_enemies.push_back(std::move(e));
            }

            void SpawnItem(const ItemDef& def, XMFLOAT3 position, const FunctionSpec& motionOverride)
            {
                Item item;
                item.def = &def;
                item.model = m_game.Models().Load(def.model.model, def.model.textureFolder);
                const FunctionSpec& motion = motionOverride.Empty() ? def.motion : motionOverride;
                item.motion = MotionRegistry::Create(motion.name, motion.params);
                if (item.motion)
                    item.motion->Begin(position, m_level.scrollSpeed);
                item.pos = position;
                m_items.push_back(std::move(item));
            }

            const EnemyDef& FallbackEnemy(const std::string& name)
            {
                auto [it, inserted] = m_fallbackEnemies.try_emplace(name);
                if (inserted)
                {
                    Log("[PlutoFall] Unknown enemy class '{}'", name);
                    it->second.name = name;
                    it->second.motion.name = "forward";
                    it->second.motion.params = Json::MakeObject();
                }
                return it->second;
            }

            const ItemDef& FallbackItem(const std::string& name)
            {
                auto [it, inserted] = m_fallbackItems.try_emplace(name);
                if (inserted)
                {
                    Log("[PlutoFall] Unknown item class '{}'", name);
                    it->second.name = name;
                    it->second.motion.name = "scroll";
                    it->second.score = 100;
                }
                return it->second;
            }

            // ------------------------------------------------------------ updates

            void UpdateShips(float dt)
            {
                Input& input = m_game.Controls();
                const GameConfig& config = m_game.Data().Config();
                for (Ship& s : m_ships)
                {
                    s.flash = std::max(0.0f, s.flash - dt);
                    s.invulnerable = std::max(0.0f, s.invulnerable - dt);
                    s.fireCooldown -= dt;
                    PlayerStats& stats = Stats(s);

                    if (!s.alive)
                    {
                        if (stats.lives > 0 && m_phase == Phase::Playing)
                        {
                            s.respawn -= dt;
                            if (s.respawn <= 0.0f)
                            {
                                s.alive = true;
                                s.pos = s.home;
                                stats.health = stats.maxHealth;
                                s.invulnerable = RespawnInvulnerability;
                            }
                        }
                        continue;
                    }

                    switch (m_phase)
                    {
                    case Phase::Intro:
                    {
                        const float t = Clamp01(m_phaseTime / IntroDuration);
                        const float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
                        s.pos = { Lerp(s.introStart.x, s.home.x, eased), Lerp(s.introStart.y, s.home.y, eased), 0.0f };
                        break;
                    }
                    case Phase::Playing:
                    {
                        const DeviceState& device = input.Device(s.device);
                        const float speed = s.def->speed;
                        s.pos.x += device.move.x * speed * dt;
                        s.pos.y += device.move.y * speed * dt;
                        const float margin = 0.7f;
                        s.pos.x = std::clamp(s.pos.x, -m_half.x + margin, m_half.x - margin);
                        s.pos.y = std::clamp(s.pos.y, -m_half.y + margin, m_half.y - margin);

                        // Bank when moving across the direction of travel.
                        const float acrossInput = device.move.x * m_axes.across.x + device.move.y * m_axes.across.y;
                        const float targetBank = (m_orientation == Orientation::Landscape ? -0.5f : 0.6f) * acrossInput;
                        s.bank += (targetBank - s.bank) * std::min(1.0f, dt * 8.0f);

                        if (device.Down(Button::Primary) && s.fireCooldown <= 0.0f)
                        {
                            s.fireCooldown = config.fireInterval;
                            FirePlayerShot(s, config.playerBullet, m_bulletModel, false);
                            m_game.Sound().PlaySfx(config.playerBullet.sound.empty() ? config.sounds.shoot : config.playerBullet.sound, 0.35f);
                        }
                        if (device.Pressed(Button::Secondary) && stats.bombs > 0)
                        {
                            --stats.bombs;
                            FirePlayerShot(s, config.playerBomb, m_bombModel, true);
                            m_game.Sound().PlaySfx(config.playerBomb.sound.empty() ? config.sounds.bomb : config.playerBomb.sound, 0.8f, 0.8f);
                        }
                        break;
                    }
                    case Phase::Exiting:
                    case Phase::FadeOut:
                        s.exitSpeed += 34.0f * dt;
                        s.pos.x += m_exit.x * s.exitSpeed * dt;
                        s.pos.y += m_exit.y * s.exitSpeed * dt;
                        s.bank *= std::max(0.0f, 1.0f - dt * 4.0f);
                        if (static_cast<int>(m_time * 30.0f) % 2 == 0)
                            Emit({ s.pos.x - m_exit.x * 0.8f, s.pos.y - m_exit.y * 0.8f, 0 }, 1, 2.0f, 0.3f, 0.25f, s.color);
                        break;
                    default:
                        break;
                    }
                }
            }

            void FirePlayerShot(const Ship& s, const ProjectileSpec& spec, const std::shared_ptr<Model>& model, bool bomb)
            {
                Shot shot;
                shot.model = model;
                shot.transform = spec.model.transform;
                shot.pos = { s.pos.x + m_exit.x * 0.9f, s.pos.y + m_exit.y * 0.9f, 0.0f };
                shot.vel = { m_exit.x * spec.speed, m_exit.y * spec.speed, 0.0f };
                shot.damage = spec.damage;
                shot.life = spec.life;
                shot.radius = spec.radius;
                shot.owner = s.slot;
                shot.fromPlayer = true;
                shot.bomb = bomb;
                m_shots.push_back(std::move(shot));
            }

            void UpdateEnemies(float dt)
            {
                const std::vector<XMFLOAT3> players = LivingPlayerPositions();
                const bool canFire = m_phase == Phase::Playing;
                for (Enemy& e : m_enemies)
                {
                    e.age += dt;
                    e.flash = std::max(0.0f, e.flash - dt);
                    if (e.motion)
                        e.pos = e.motion->Evaluate(e.age, m_orientation);

                    if (!e.entered && InField(e.pos, -0.5f))
                        e.entered = true;

                    if (e.boss)
                    {
                        // Bosses stay on screen until destroyed.
                        if (e.entered)
                        {
                            e.pos.x = std::clamp(e.pos.x, -m_half.x + 1.0f, m_half.x - 1.0f);
                            e.pos.y = std::clamp(e.pos.y, -m_half.y + 1.0f, m_half.y - 1.0f);
                        }
                    }
                    else if (!InField(e.pos, CullMargin) && (e.entered || e.age > 15.0f))
                    {
                        e.dead = true; // drifted well outside the playfield
                        continue;
                    }

                    if (canFire && e.fire && InField(e.pos, -0.3f))
                    {
                        if (auto volley = e.fire->Evaluate(e.age, e.pos, players, m_orientation))
                        {
                            for (const ProjectileSpawn& spawn : *volley)
                            {
                                Shot shot;
                                shot.model = e.projectileModel;
                                shot.transform = e.def->projectile.model.transform;
                                shot.pos = spawn.position;
                                shot.vel = spawn.velocity;
                                shot.damage = e.def->projectile.damage;
                                shot.life = e.def->projectile.life;
                                shot.radius = e.def->projectile.radius;
                                m_shots.push_back(std::move(shot));
                            }
                            if (!e.def->projectile.sound.empty())
                                m_game.Sound().PlaySfx(e.def->projectile.sound, 0.4f);
                        }
                    }
                }
            }

            void UpdateItems(float dt)
            {
                for (Item& item : m_items)
                {
                    item.age += dt;
                    if (item.motion)
                        item.pos = item.motion->Evaluate(item.age, m_orientation);
                    if (!item.entered && InField(item.pos, -0.5f))
                        item.entered = true;
                    if (!InField(item.pos, CullMargin) && (item.entered || item.age > 15.0f))
                        item.dead = true;
                }
            }

            void UpdateShots(float dt)
            {
                for (Shot& s : m_shots)
                {
                    s.age += dt;
                    s.pos.x += s.vel.x * dt;
                    s.pos.y += s.vel.y * dt;
                    if (!InField(s.pos, 1.5f))
                        s.dead = true;
                }
            }

            void UpdateParticles(float dt)
            {
                for (Particle& p : m_particles)
                {
                    p.life -= dt;
                    p.pos.x += p.vel.x * dt;
                    p.pos.y += p.vel.y * dt;
                    p.pos.z += p.vel.z * dt;
                    p.vel.x *= (1.0f - dt * 1.5f);
                    p.vel.y *= (1.0f - dt * 1.5f);
                }
                std::erase_if(m_particles, [](const Particle& p) { return p.life <= 0.0f; });
            }

            void UpdatePause(float dt)
            {
                Input& input = m_game.Controls();
                m_pauseMenu.Navigate(m_game, 2, dt);
                if (input.AnyPressed(Button::Menu) || input.AnyPressed(Button::Cancel))
                {
                    m_paused = false;
                    return;
                }
                if (input.AnyConfirm())
                {
                    m_game.PlayMenuSelect();
                    if (m_pauseMenu.selected == 0)
                    {
                        m_paused = false;
                    }
                    else if (!m_leaving)
                    {
                        m_leaving = true;
                        m_game.ShowTitle();
                    }
                }
            }

            // ------------------------------------------------------------ collisions & effects

            void Collide()
            {
                const GameConfig& config = m_game.Data().Config();

                // Player shots vs enemy projectiles and enemies.
                for (Shot& shot : m_shots)
                {
                    if (shot.dead || !shot.fromPlayer)
                        continue;

                    if (!shot.bomb)
                    {
                        for (Shot& other : m_shots)
                        {
                            if (other.dead || other.fromPlayer || !Overlaps(shot.pos, shot.radius, other.pos, other.radius))
                                continue;
                            other.life -= shot.damage;
                            shot.dead = true;
                            if (other.life <= 0.0f)
                            {
                                other.dead = true;
                                Emit(other.pos, 5, 4.0f, 0.25f, 0.18f, Color{ 1.0f, 0.7f, 0.3f, 1 });
                            }
                            break;
                        }
                        if (shot.dead)
                            continue;
                    }

                    for (Enemy& e : m_enemies)
                    {
                        if (e.dead || e.life <= 0.0f || !Overlaps(shot.pos, shot.radius, e.pos, e.def->radius))
                            continue;
                        shot.dead = true;
                        if (shot.bomb)
                        {
                            Explode(shot.pos, shot.owner, config.bombRadius, config.bombDamage);
                        }
                        else
                        {
                            e.life -= shot.damage;
                            e.flash = 0.06f;
                            e.lastHitBy = shot.owner;
                            Emit(shot.pos, 3, 5.0f, 0.2f, 0.15f, Color{ 0.6f, 0.95f, 1.0f, 1 });
                            m_game.Sound().PlaySfx(config.sounds.hit, 0.3f);
                        }
                        break;
                    }
                }

                // Enemy projectiles, enemies and items vs players.
                for (Ship& ship : m_ships)
                {
                    if (!ship.alive)
                        continue;
                    const float radius = ship.def->radius;
                    for (Item& item : m_items)
                    {
                        if (!item.dead && Overlaps(ship.pos, radius, item.pos, item.def->radius))
                        {
                            Collect(ship, item);
                            item.dead = true;
                        }
                    }
                    if (ship.invulnerable > 0.0f)
                        continue;
                    for (Shot& shot : m_shots)
                    {
                        if (shot.dead || shot.fromPlayer || !Overlaps(ship.pos, radius, shot.pos, shot.radius))
                            continue;
                        shot.dead = true;
                        DamagePlayer(ship, shot.damage);
                        break;
                    }
                    if (!ship.alive || ship.invulnerable > 0.0f)
                        continue;
                    for (Enemy& e : m_enemies)
                    {
                        if (e.dead || !Overlaps(ship.pos, radius, e.pos, e.def->radius))
                            continue;
                        DamagePlayer(ship, e.def->contactDamage);
                        if (!e.boss)
                        {
                            e.life -= 4.0f;
                            e.lastHitBy = ship.slot;
                        }
                        break;
                    }
                }
            }

            void Explode(XMFLOAT3 center, int owner, float radius, float damage)
            {
                const GameConfig& config = m_game.Data().Config();
                for (Enemy& e : m_enemies)
                {
                    if (e.dead || !Overlaps(center, radius, e.pos, e.def->radius))
                        continue;
                    e.life -= damage;
                    e.flash = 0.12f;
                    e.lastHitBy = owner;
                }
                for (Shot& s : m_shots)
                {
                    if (!s.fromPlayer && Overlaps(center, radius, s.pos, s.radius))
                        s.dead = true;
                }
                // Expanding ring of particles.
                for (int i = 0; i < 48; ++i)
                {
                    const float angle = 2.0f * Pi * static_cast<float>(i) / 48.0f;
                    const float speed = radius / 0.35f;
                    m_particles.push_back({ center, { std::cos(angle) * speed, std::sin(angle) * speed, 0 }, 0.5f, 0.5f, 0.35f,
                                            Color{ 1.0f, 0.85f, 0.3f, 1 } });
                }
                Emit(center, 20, 6.0f, 0.7f, 0.4f, Color{ 1.0f, 0.5f, 0.15f, 1 });
                m_game.Sound().PlaySfx(config.sounds.bigExplosion, 0.9f);
                RumbleSlot(owner, 0.8f, 0.6f, 0.35f);
            }

            void KillEnemy(Enemy& e)
            {
                e.dead = true;
                const GameConfig& config = m_game.Data().Config();
                const bool big = e.boss;
                Emit(e.pos, big ? 90 : 26, big ? 12.0f : 7.0f, big ? 1.4f : 0.7f, big ? 0.6f : 0.35f, Color{ 1.0f, 0.55f, 0.2f, 1 });
                Emit(e.pos, big ? 40 : 10, big ? 8.0f : 4.0f, big ? 1.0f : 0.5f, big ? 0.5f : 0.3f, Color{ 1.0f, 0.95f, 0.7f, 1 });
                const std::string& sound = !e.def->deathSound.empty() ? e.def->deathSound
                                                                     : (big ? config.sounds.bigExplosion : config.sounds.explosion);
                m_game.Sound().PlaySfx(sound, big ? 1.0f : 0.7f, RandomFloat(0.9f, 1.1f));

                if (e.lastHitBy >= 0 && e.lastHitBy < static_cast<int>(m_ships.size()))
                    Stats(m_ships[static_cast<size_t>(e.lastHitBy)]).score += e.def->score;
                if (big)
                {
                    for (const Ship& s : m_ships)
                        m_game.Controls().Rumble(s.device, 1.0f, 1.0f, 0.6f);
                }
                if (!e.def->drop.empty())
                {
                    if (const ItemDef* item = m_game.Data().Item(e.def->drop))
                    {
                        FunctionSpec motion;
                        motion.name = "scroll";
                        motion.params = Json::MakeObject();
                        SpawnItem(*item, e.pos, motion);
                    }
                }
            }

            void DamagePlayer(Ship& ship, float amount)
            {
                const GameConfig& config = m_game.Data().Config();
                PlayerStats& stats = Stats(ship);
                stats.health -= std::max(1, static_cast<int>(std::ceil(amount)));
                ship.flash = 0.15f;
                ship.invulnerable = HitInvulnerability;
                m_game.Controls().Rumble(ship.device, 0.6f, 0.9f, 0.25f);
                m_game.Sound().PlaySfx(config.sounds.playerHit, 0.8f);
                Emit(ship.pos, 8, 5.0f, 0.3f, 0.2f, Color{ 1.0f, 0.4f, 0.3f, 1 });

                if (stats.health > 0)
                    return;

                // Lost a life.
                stats.health = 0;
                stats.lives = std::max(0, stats.lives - 1);
                ship.alive = false;
                ship.respawn = RespawnDelay;
                Emit(ship.pos, 60, 10.0f, 1.2f, 0.5f, ship.color);
                Emit(ship.pos, 30, 6.0f, 0.9f, 0.4f, Color{ 1.0f, 0.9f, 0.6f, 1 });
                m_game.Sound().PlaySfx(config.sounds.bigExplosion, 1.0f);
                m_game.Controls().Rumble(ship.device, 1.0f, 1.0f, 0.7f);

                if (stats.lives == 0)
                {
                    const bool anyLeft = std::ranges::any_of(m_ships, [&](const Ship& s) { return Stats(s).lives > 0; });
                    if (config.gameOverWhenAnyPlayerDies || !anyLeft)
                        SetPhase(Phase::GameOver);
                }
            }

            void Collect(Ship& ship, const Item& item)
            {
                const GameConfig& config = m_game.Data().Config();
                PlayerStats& stats = Stats(ship);
                const ItemDef& def = *item.def;
                stats.maxHealth = std::max(1, stats.maxHealth + def.maxHealth);
                stats.health = std::clamp(stats.health + def.health + std::max(0, def.maxHealth), 0, stats.maxHealth);
                stats.lives += def.lives;
                stats.bombs += def.bombs;
                stats.score += def.score;
                const std::string& sound = !def.sound.empty() ? def.sound : (def.lives > 0 ? config.sounds.extraLife : config.sounds.powerup);
                m_game.Sound().PlaySfx(sound, 0.9f);
                Emit(item.pos, 16, 5.0f, 0.5f, 0.25f, Color{ 0.5f, 1.0f, 0.6f, 1 });
                m_game.Controls().Rumble(ship.device, 0.2f, 0.4f, 0.15f);
            }

            void Emit(XMFLOAT3 at, int count, float speed, float life, float size, const Color& color)
            {
                for (int i = 0; i < count; ++i)
                {
                    const float angle = RandomFloat(0.0f, 2.0f * Pi);
                    const float v = speed * RandomFloat(0.25f, 1.0f);
                    const float l = life * RandomFloat(0.6f, 1.0f);
                    m_particles.push_back({ at, { std::cos(angle) * v, std::sin(angle) * v, RandomFloat(-2.0f, 2.0f) }, l, l,
                                            size * RandomFloat(0.6f, 1.2f), color });
                }
            }

            void RumbleSlot(int slot, float low, float high, float seconds)
            {
                if (slot >= 0 && slot < static_cast<int>(m_ships.size()))
                    m_game.Controls().Rumble(m_ships[static_cast<size_t>(slot)].device, low, high, seconds);
            }

            void CompleteLevel()
            {
                if (m_leaving)
                    return;
                m_leaving = true;
                Session& session = m_game.CurrentSession();
                ++session.levelsCompleted;
                if (m_level.terminal || m_level.nextDialogs.empty())
                {
                    m_game.ShowTitle();
                    return;
                }
                // Random choice for now; this is the hook for choosing based on level events.
                const auto& options = m_level.nextDialogs;
                m_game.ShowDialog(options[static_cast<size_t>(RandomInt(0, static_cast<int>(options.size()) - 1))]);
            }

            // ------------------------------------------------------------ 2D drawing

            void DrawScrollingImage(Ui& ui)
            {
                ID2D1Bitmap* bitmap = ui.Images().Get(m_level.backgroundImage);
                const D2D1_SIZE_F size = bitmap->GetSize();
                const float pixelsPerUnit = Graphics::CanvasWidth / (Renderer3D::ViewHalfWidth * 2.0f);
                const float offset = m_scroll * pixelsPerUnit * m_level.backgroundParallax;
                if (m_orientation == Orientation::Landscape)
                {
                    const float h = Graphics::CanvasHeight;
                    const float w = size.height > 1.0f ? std::max(64.0f, size.width * h / size.height) : Graphics::CanvasWidth;
                    float x = -std::fmod(offset, w);
                    for (; x < Graphics::CanvasWidth; x += w)
                        ui.Image(bitmap, D2D1::RectF(x, 0, x + w, h));
                }
                else
                {
                    const float w = Graphics::CanvasWidth;
                    const float h = size.width > 1.0f ? std::max(64.0f, size.height * w / size.width) : Graphics::CanvasHeight;
                    float y = std::fmod(offset, h) - h;
                    for (; y < Graphics::CanvasHeight; y += h)
                        ui.Image(bitmap, D2D1::RectF(0, y, w, y + h));
                }
            }

            D2D1_RECT_F FieldRect() const
            {
                const D2D1_POINT_2F tl = Renderer3D::WorldToCanvas({ -m_half.x, m_half.y, 0 });
                const D2D1_POINT_2F br = Renderer3D::WorldToCanvas({ m_half.x, -m_half.y, 0 });
                return D2D1::RectF(tl.x, tl.y, br.x, br.y);
            }

            void DrawPortraitFrame(Ui& ui)
            {
                const D2D1_RECT_F field = FieldRect();
                const Color panel{ 0.03f, 0.04f, 0.09f, 1.0f };
                ui.FillRect(D2D1::RectF(0, 0, field.left, Graphics::CanvasHeight), panel);
                ui.FillRect(D2D1::RectF(field.right, 0, Graphics::CanvasWidth, Graphics::CanvasHeight), panel);
                ui.Line({ field.left, 0 }, { field.left, Graphics::CanvasHeight }, Color{ 0.45f, 0.7f, 1.0f, 0.8f }, 3.0f);
                ui.Line({ field.right, 0 }, { field.right, Graphics::CanvasHeight }, Color{ 0.45f, 0.7f, 1.0f, 0.8f }, 3.0f);
            }

            D2D1_RECT_F HudRect(int index) const
            {
                if (m_orientation == Orientation::Landscape)
                {
                    constexpr float Width = 440.0f, Gap = 20.0f;
                    const float left = 20.0f + index * (Width + Gap);
                    return D2D1::RectF(left, 14, left + Width, 124);
                }
                const D2D1_RECT_F field = FieldRect();
                const bool leftSide = index % 2 == 0;
                const float x0 = leftSide ? 20.0f : field.right + 20.0f;
                const float x1 = leftSide ? field.left - 20.0f : Graphics::CanvasWidth - 20.0f;
                const float y0 = 40.0f + (index / 2) * 200.0f;
                return D2D1::RectF(x0, y0, x1, y0 + 170.0f);
            }

            void DrawHud(Ui& ui)
            {
                for (const Ship& ship : m_ships)
                {
                    const PlayerStats& stats = m_game.CurrentSession().players[static_cast<size_t>(ship.slot)].stats;
                    const D2D1_RECT_F rect = HudRect(ship.slot);
                    ui.FillRoundRect(rect, 10.0f, Color{ 0.02f, 0.03f, 0.08f, 0.6f });
                    ui.DrawRoundRect(rect, 10.0f, ship.color.WithAlpha(0.8f), 2.0f);

                    const float pad = 14.0f;
                    TextStyle label;
                    label.size = 28.0f;
                    label.bold = true;
                    label.color = ship.color;
                    label.wrap = false;
                    ui.Text(std::format(L"P{}", ship.slot + 1), D2D1::RectF(rect.left + pad, rect.top + 6, rect.left + 80, rect.top + 42), label);

                    TextStyle score = label;
                    score.align = TextAlign::Right;
                    score.color = Colors::White;
                    ui.Text(std::format(L"{:08}", stats.score), D2D1::RectF(rect.left + 80, rect.top + 6, rect.right - pad, rect.top + 42), score);

                    const float meterTop = rect.top + 48.0f;
                    const float fraction = stats.maxHealth > 0 ? static_cast<float>(stats.health) / stats.maxHealth : 0.0f;
                    const Color healthColor = fraction > 0.5f ? Colors::Green : fraction > 0.25f ? Colors::Gold : Colors::Red;
                    ui.Meter(D2D1::RectF(rect.left + pad, meterTop, rect.right - pad, meterTop + 18.0f), fraction, healthColor,
                             Color{ 0.15f, 0.05f, 0.05f, 0.8f });

                    TextStyle small;
                    small.size = 22.0f;
                    small.wrap = false;
                    small.color = Color{ 0.85f, 0.9f, 1.0f, 1.0f };
                    const std::wstring info = std::format(L"{} {}   {} {}   {} {}/{}", Localization::GetW("hud.lives"), stats.lives,
                                                          Localization::GetW("hud.bombs"), stats.bombs,
                                                          Localization::GetW("hud.health"), stats.health, stats.maxHealth);
                    ui.Text(info, D2D1::RectF(rect.left + pad, meterTop + 22.0f, rect.right - pad, meterTop + 52.0f), small);

                    if (!ship.alive && stats.lives > 0 && m_phase == Phase::Playing)
                    {
                        TextStyle respawn = small;
                        respawn.color = Colors::Gold;
                        ui.Text(Localization::GetW("hud.respawning"),
                                D2D1::RectF(rect.left + pad, meterTop + 50.0f, rect.right - pad, meterTop + 80.0f), respawn);
                    }
                }

                // Boss health.
                float bossLife = 0.0f, bossMax = 0.0f;
                for (const Enemy& e : m_enemies)
                {
                    if (e.boss && !e.dead)
                    {
                        bossLife += std::max(0.0f, e.life);
                        bossMax += e.maxLife;
                    }
                }
                if (bossMax > 0.0f)
                {
                    const D2D1_RECT_F field = FieldRect();
                    const float top = m_orientation == Orientation::Landscape ? 140.0f : 20.0f;
                    const D2D1_RECT_F bar = D2D1::RectF(field.left + 160.0f, top + 30.0f, field.right - 160.0f, top + 52.0f);
                    TextStyle label;
                    label.size = 24.0f;
                    label.bold = true;
                    label.align = TextAlign::Center;
                    label.color = Colors::Red;
                    label.shadow = true;
                    ui.Text(Localization::GetW("hud.boss"), D2D1::RectF(bar.left, top, bar.right, top + 30.0f), label);
                    ui.Meter(bar, bossLife / bossMax, Color{ 0.95f, 0.2f, 0.25f, 1 }, Color{ 0.1f, 0.0f, 0.0f, 0.7f });
                }
            }

            void DrawBanners(Ui& ui)
            {
                const D2D1_RECT_F field = FieldRect();
                if (m_phase == Phase::Intro || (m_phase == Phase::Playing && m_phaseTime < 1.5f))
                {
                    const float alpha = m_phase == Phase::Intro ? Clamp01(m_phaseTime * 2.0f) : Clamp01(1.0f - m_phaseTime / 1.5f);
                    TextStyle style;
                    style.size = 72.0f;
                    style.bold = true;
                    style.align = TextAlign::Center;
                    style.verticalCenter = true;
                    style.shadow = true;
                    style.color = Color{ 0.85f, 0.93f, 1.0f, alpha };
                    ui.Text(m_level.name.GetW(), D2D1::RectF(field.left, 380, field.right, 520), style);
                }
                if (m_bannerTime > 0.0f && std::fmod(m_bannerTime, 0.5f) > 0.15f)
                {
                    TextStyle style;
                    style.size = 90.0f;
                    style.bold = true;
                    style.align = TextAlign::Center;
                    style.verticalCenter = true;
                    style.shadow = true;
                    style.color = Colors::Red;
                    ui.FillRect(D2D1::RectF(field.left, 470, field.right, 610), Color{ 0.3f, 0.0f, 0.0f, 0.45f });
                    ui.Text(m_bannerText, D2D1::RectF(field.left, 470, field.right, 610), style);
                }
                if (m_phase == Phase::Exiting || m_phase == Phase::FadeOut)
                {
                    TextStyle style;
                    style.size = 72.0f;
                    style.bold = true;
                    style.align = TextAlign::Center;
                    style.verticalCenter = true;
                    style.shadow = true;
                    style.color = Colors::Gold;
                    ui.Text(Localization::GetW("game.clear"), D2D1::RectF(field.left, 400, field.right, 540), style);
                }
            }

            void DrawPause(Ui& ui)
            {
                ui.FillScreen(Color{ 0, 0, 0, 0.55f });
                const D2D1_RECT_F panel = D2D1::RectF(610, 330, 1310, 750);
                DrawPanel(ui, panel, 0.85f);
                TextStyle title;
                title.size = 64.0f;
                title.bold = true;
                title.align = TextAlign::Center;
                title.shadow = true;
                ui.Text(Localization::GetW("pause.title"), D2D1::RectF(panel.left, panel.top + 30, panel.right, panel.top + 120), title);
                const std::array<std::wstring, 2> items = { Localization::GetW("pause.resume"), Localization::GetW("pause.quit") };
                m_pauseMenu.Render(ui, items, (panel.left + panel.right) * 0.5f, panel.top + 170, 90.0f, 42.0f, m_game.TotalTime());
            }

            // ------------------------------------------------------------ data

            std::string m_levelPath;
            LevelDef m_level;
            bool m_failed = false;
            bool m_leaving = false;
            Orientation m_orientation = Orientation::Landscape;
            Axes m_axes{};
            XMFLOAT2 m_exit{ 1, 0 };
            XMFLOAT2 m_half{ 16, 9 };

            Phase m_phase = Phase::Intro;
            float m_phaseTime = 0.0f;
            float m_time = 0.0f;
            float m_scroll = 0.0f;
            float m_fade = 0.0f;
            size_t m_nextSpawn = 0;
            bool m_paused = false;
            MenuList m_pauseMenu;
            float m_bannerTime = 0.0f;
            std::wstring m_bannerText;

            std::shared_ptr<Model> m_bulletModel;
            std::shared_ptr<Model> m_bombModel;
            std::shared_ptr<Model> m_particleModel;

            std::vector<Ship> m_ships;
            std::vector<Enemy> m_enemies;
            std::vector<Item> m_items;
            std::vector<Shot> m_shots;
            std::vector<Particle> m_particles;
            std::vector<Backdrop> m_backdrops;
            std::map<std::string, EnemyDef> m_fallbackEnemies; // unknown classes appear as random coloured cubes
            std::map<std::string, ItemDef> m_fallbackItems;
        };
    }

    std::unique_ptr<Screen> MakeGameplayScreen(Game& game, const std::string& levelPath)
    {
        return std::make_unique<GameplayScreen>(game, levelPath);
    }
}
