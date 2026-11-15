// GameData.h - data definitions loaded from JSON: game configuration, enemy/item types,
// levels, dialogs and credits. See README.md for the file formats.
#pragma once
#include "Common.h"
#include "Json.h"
#include "Localization.h"

namespace pf
{
    // A model reference: glTF/GLB file, the folder its textures live in and a transform.
    struct ModelSpec
    {
        std::string model;
        std::string textureFolder;
        XMFLOAT4X4 transform{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

        // Reads "model", "textureFolder" and "transform" from an object.
        static ModelSpec FromJson(const Json& json);
        XMMATRIX Transform() const { return XMLoadFloat4x4(&transform); }
    };

    // Parses a transform: either 16 numbers (row-major, row-vector convention as used by
    // DirectXMath) or { "scale": s|[x,y,z], "rotation": [pitch,yaw,roll] degrees, "translation": [x,y,z] }.
    XMFLOAT4X4 ParseTransform(const Json& json);

    // Name of a motion/fire function plus its parameters. Accepts "name" or { "name": "...", ...params }.
    struct FunctionSpec
    {
        std::string name;
        Json params;
        static FunctionSpec FromJson(const Json& json);
        bool Empty() const noexcept { return name.empty(); }
    };

    struct ProjectileSpec
    {
        ModelSpec model;     // empty model = glowing box
        Color color{ 1.0f, 0.55f, 0.15f, 1.0f };
        XMFLOAT3 size{ 0.45f, 0.18f, 0.18f };
        float damage = 1.0f;
        float life = 1.0f;   // projectiles can be shot down
        float radius = 0.22f;
        float speed = 22.0f; // used for player projectiles
        std::string sound;

        static ProjectileSpec FromJson(const Json& json, const ProjectileSpec& defaults);
    };

    struct EnemyDef
    {
        std::string name;
        ModelSpec model;
        float life = 3.0f;
        float radius = 0.8f;
        int score = 100;
        bool boss = false;
        float contactDamage = 2.0f;
        FunctionSpec motion;
        FunctionSpec fire;
        ProjectileSpec projectile;
        std::string deathSound;
        std::string drop;    // optional item class dropped on death
    };

    struct ItemDef
    {
        std::string name;
        ModelSpec model;
        float radius = 0.8f;
        int lives = 0;
        int health = 0;
        int maxHealth = 0;
        int bombs = 0;
        int score = 0;
        FunctionSpec motion;
        std::string sound;
        float spin = 90.0f; // degrees per second
        Color glow{ 0.2f, 0.2f, 0.2f, 1.0f };
    };

    struct PlayerModelDef
    {
        LocString name;
        ModelSpec model;
        float speed = 11.0f;
        float radius = 0.45f;
        Color color = Colors::Cyan;
    };

    struct GameSounds
    {
        std::string shoot, bomb, explosion, bigExplosion, hit, playerHit, powerup, menuMove, menuSelect, gameOver, extraLife;
    };

    struct GameConfig
    {
        std::string titleImage;
        std::string titleMusic;
        std::string startDialog;
        std::string credits;
        std::string website = "https://j2i.net/apps/pluto-fall";
        std::string selectBackground;
        int startLives = 3;
        int startHealth = 10;
        int startBombs = 3;
        float fireInterval = 0.11f;
        float bombRadius = 5.0f;
        float bombDamage = 12.0f;
        bool gameOverWhenAnyPlayerDies = true;
        ProjectileSpec playerBullet;
        ProjectileSpec playerBomb;
        std::vector<PlayerModelDef> players;
        GameSounds sounds;
    };

    // Owns all type definitions.
    class GameData
    {
    public:
        bool Load();
        const GameConfig& Config() const noexcept { return m_config; }
        const EnemyDef* Enemy(std::string_view name) const;
        const ItemDef* Item(std::string_view name) const;

    private:
        GameConfig m_config;
        std::unordered_map<std::string, EnemyDef> m_enemies;
        std::unordered_map<std::string, ItemDef> m_items;
    };

    // ---------------------------------------------------------------- levels

    struct SpawnDef
    {
        bool isItem = false;
        XMFLOAT2 at{ 0, 0 };        // scroll position at which the object becomes active
        std::string className;
        XMFLOAT3 position{ 0, 0, 0 }; // start position relative to the visible playfield centre
        FunctionSpec motion;        // optional override
        FunctionSpec fire;          // optional override (enemies)
        std::optional<bool> boss;
        std::optional<float> life;

        // Distance along the scroll axis at which this spawn activates.
        float Activation(Orientation orientation) const { return orientation == Orientation::Landscape ? at.x : at.y; }
    };

    struct BackgroundDef
    {
        ModelSpec model;
        XMFLOAT3 position{ 0, 0, 10 }; // level (world) coordinates; z > 0 is further from the camera
        FunctionSpec motion;
        XMFLOAT3 spin{ 0, 0, 0 };      // degrees per second around x, y, z
    };

    struct LevelDef
    {
        std::string path;
        LocString name;
        Orientation orientation = Orientation::Landscape;
        float length = 200.0f;
        float scrollSpeed = 3.0f;
        bool terminal = false;
        std::string music;
        Color backgroundColor{ 0.01f, 0.01f, 0.04f, 1.0f };
        std::string backgroundImage;
        float backgroundParallax = 0.25f;
        std::vector<std::string> nextDialogs;
        std::vector<SpawnDef> spawns;
        std::vector<BackgroundDef> background;

        static std::optional<LevelDef> Load(std::string_view relativePath);
    };

    // ---------------------------------------------------------------- dialogs

    struct DialogResponse
    {
        std::string id;
        LocString text;
        std::string gotoSection; // optional
    };

    struct DialogEntry
    {
        LocString speaker;
        std::string portrait;
        LocString text;
        std::string key;                      // question key (only if responses are present)
        std::vector<DialogResponse> responses;
    };

    struct DialogSection
    {
        std::string id;
        std::string background;
        std::vector<DialogEntry> entries;
        bool terminal = false;
    };

    struct NextLevelOption
    {
        std::string key;
        std::string level;
    };

    struct DialogDef
    {
        std::string path;
        std::string typeSound;
        std::string music;
        float charactersPerSecond = 40.0f;
        Color boxColor{ 0.02f, 0.04f, 0.12f, 0.78f };
        Color borderColor{ 0.45f, 0.75f, 1.0f, 1.0f };
        Color textColor = Colors::White;
        std::vector<DialogSection> sections;
        std::vector<NextLevelOption> nextLevels;

        static std::optional<DialogDef> Load(std::string_view relativePath);
        int FindSection(std::string_view id) const;
    };

    // Chooses the level to load after a dialog based on the player's answers.
    //
    // Each option's key is a list of conditions separated by '&' or ',', each of the form
    // "questionKey=responseId" (or "questionKey!=responseId"). The first option whose conditions all
    // hold is chosen. Options with the key "default", "*" or "" match anything and are used if nothing
    // more specific matches. If no option matches, the first option is returned.
    std::string SelectNextLevel(const std::map<std::string, std::string>& choices, std::span<const NextLevelOption> options);

    // ---------------------------------------------------------------- credits

    struct CreditLine
    {
        std::string value;
        Color color = Colors::White;
    };

    struct CreditSection
    {
        LocString title;
        std::vector<CreditLine> contributors;
    };

    std::vector<CreditSection> LoadCredits(std::string_view relativePath);

    // ---------------------------------------------------------------- session

    struct PlayerStats
    {
        int lives = 3;
        int health = 10;
        int maxHealth = 10;
        int bombs = 3;
        int64_t score = 0;
    };

    struct PlayerSlot
    {
        int device = 0;     // Input device index
        int modelIndex = 0; // index into GameConfig::players
        PlayerStats stats;
    };

    // State carried between screens for one play-through.
    struct Session
    {
        std::vector<PlayerSlot> players;
        std::map<std::string, std::string> choices;
        int levelsCompleted = 0;

        void Reset()
        {
            players.clear();
            choices.clear();
            levelsCompleted = 0;
        }
    };
}
