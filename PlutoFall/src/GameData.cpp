#include "GameData.h"
#include "Util.h"

namespace pf
{
    namespace
    {
        XMFLOAT3 ReadVec3(const Json& json, XMFLOAT3 fallback)
        {
            if (json.IsNumber())
                return { json.AsFloat(), json.AsFloat(), json.AsFloat() };
            if (json.Size() >= 3)
                return { json[0].AsFloat(fallback.x), json[1].AsFloat(fallback.y), json[2].AsFloat(fallback.z) };
            if (json.Size() == 2)
                return { json[0].AsFloat(fallback.x), json[1].AsFloat(fallback.y), fallback.z };
            if (json.IsObject())
                return { json.GetFloat("x", fallback.x), json.GetFloat("y", fallback.y), json.GetFloat("z", fallback.z) };
            return fallback;
        }

        XMFLOAT2 ReadVec2(const Json& json, XMFLOAT2 fallback)
        {
            if (json.IsNumber())
                return { json.AsFloat(), json.AsFloat() };
            if (json.Size() >= 2)
                return { json[0].AsFloat(fallback.x), json[1].AsFloat(fallback.y) };
            if (json.IsObject())
                return { json.GetFloat("x", fallback.x), json.GetFloat("y", fallback.y) };
            return fallback;
        }

        Color ReadColor(const Json& json, Color fallback)
        {
            if (json.IsString())
                return ParseColor(json.AsString()).value_or(fallback);
            if (json.Size() >= 3)
                return { json[0].AsFloat(), json[1].AsFloat(), json[2].AsFloat(), json.Size() >= 4 ? json[3].AsFloat(1) : 1.0f };
            return fallback;
        }

        std::vector<std::string> ReadStringList(const Json& json)
        {
            std::vector<std::string> list;
            if (json.IsString())
                list.push_back(json.AsString());
            for (const Json& item : json.Items())
            {
                if (item.IsString())
                    list.push_back(item.AsString());
                else if (item.IsObject())
                    list.push_back(item.GetString("file", item.GetString("dialog")));
            }
            std::erase_if(list, [](const std::string& s) { return s.empty(); });
            return list;
        }

        // Loads every *.json file in a folder. Each file may hold one definition object or an array of them.
        void ForEachDefinition(std::string_view folder, const std::function<void(const Json&, const std::string&)>& fn)
        {
            const fs::path dir = Paths::Asset(folder);
            std::error_code ec;
            if (!fs::is_directory(dir, ec))
                return;
            std::vector<fs::path> files;
            for (const auto& entry : fs::directory_iterator(dir, ec))
            {
                if (entry.is_regular_file() && ToLower(Narrow(entry.path().extension().wstring())) == ".json")
                    files.push_back(entry.path());
            }
            std::ranges::sort(files);
            for (const auto& file : files)
            {
                auto json = Json::Load(file);
                if (!json)
                    continue;
                const std::string stem = Narrow(file.stem().wstring());
                if (json->IsArray())
                {
                    for (const Json& item : json->Items())
                        fn(item, stem);
                }
                else
                {
                    fn(*json, stem);
                }
            }
        }

        GameSounds ReadSounds(const Json& j)
        {
            GameSounds s;
            s.shoot = j.GetString("shoot", "sounds/shoot.wav");
            s.bomb = j.GetString("bomb", "sounds/bomb.wav");
            s.explosion = j.GetString("explosion", "sounds/explosion.wav");
            s.bigExplosion = j.GetString("bigExplosion", "sounds/big_explosion.wav");
            s.hit = j.GetString("hit", "sounds/hit.wav");
            s.playerHit = j.GetString("playerHit", "sounds/player_hit.wav");
            s.powerup = j.GetString("powerup", "sounds/powerup.wav");
            s.menuMove = j.GetString("menuMove", "sounds/menu_move.wav");
            s.menuSelect = j.GetString("menuSelect", "sounds/menu_select.wav");
            s.gameOver = j.GetString("gameOver", "sounds/game_over.wav");
            s.extraLife = j.GetString("extraLife", s.powerup);
            return s;
        }
    }

    // ---------------------------------------------------------------- specs

    XMFLOAT4X4 ParseTransform(const Json& json)
    {
        XMFLOAT4X4 result;
        XMStoreFloat4x4(&result, XMMatrixIdentity());
        if (json.Size() == 16 && json.IsArray())
        {
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c)
                    result.m[r][c] = json[r * 4 + c].AsFloat();
            return result;
        }
        if (!json.IsObject())
            return result;
        if (json["matrix"].Size() == 16)
            return ParseTransform(json["matrix"]);
        const XMFLOAT3 scale = ReadVec3(json["scale"], { 1, 1, 1 });
        const XMFLOAT3 rotation = ReadVec3(json["rotation"], { 0, 0, 0 });
        const XMFLOAT3 translation = ReadVec3(json["translation"], { 0, 0, 0 });
        const XMMATRIX m = XMMatrixScaling(scale.x, scale.y, scale.z) *
                           XMMatrixRotationRollPitchYaw(XMConvertToRadians(rotation.x), XMConvertToRadians(rotation.y),
                                                        XMConvertToRadians(rotation.z)) *
                           XMMatrixTranslation(translation.x, translation.y, translation.z);
        XMStoreFloat4x4(&result, m);
        return result;
    }

    ModelSpec ModelSpec::FromJson(const Json& json)
    {
        ModelSpec spec;
        spec.model = json.GetString("model");
        spec.textureFolder = json.GetString("textureFolder");
        spec.transform = ParseTransform(json["transform"]);
        return spec;
    }

    FunctionSpec FunctionSpec::FromJson(const Json& json)
    {
        FunctionSpec spec;
        if (json.IsString())
        {
            spec.name = json.AsString();
            spec.params = Json::MakeObject();
        }
        else if (json.IsObject())
        {
            spec.name = json.GetString("name");
            // Parameters may be nested in "params" or given inline next to "name".
            spec.params = json["params"].IsObject() ? json["params"] : json;
        }
        return spec;
    }

    ProjectileSpec ProjectileSpec::FromJson(const Json& json, const ProjectileSpec& defaults)
    {
        ProjectileSpec spec = defaults;
        if (!json.IsObject())
            return spec;
        if (json.Has("model"))
            spec.model = ModelSpec::FromJson(json);
        spec.color = ReadColor(json["color"], spec.color);
        spec.size = ReadVec3(json["size"], spec.size);
        spec.damage = json.GetFloat("damage", spec.damage);
        spec.life = json.GetFloat("life", spec.life);
        spec.radius = json.GetFloat("radius", spec.radius);
        spec.speed = json.GetFloat("speed", spec.speed);
        spec.sound = json.GetString("sound", spec.sound);
        return spec;
    }

    // ---------------------------------------------------------------- GameData

    bool GameData::Load()
    {
        auto json = Json::Load(Paths::Asset("data/game.json"));
        const Json root = json ? *json : Json::MakeObject();

        GameConfig& c = m_config;
        c.titleImage = root.GetString("titleImage", "images/title.png");
        c.titleMusic = root.GetString("titleMusic");
        c.startDialog = root.GetString("startDialog", "dialog/intro.json");
        c.credits = root.GetString("credits", "data/credits.json");
        c.website = root.GetString("website", c.website);
        c.selectBackground = root.GetString("selectBackground", c.titleImage);
        c.startLives = root.GetInt("startLives", c.startLives);
        c.startHealth = root.GetInt("startHealth", c.startHealth);
        c.startBombs = root.GetInt("startBombs", c.startBombs);
        c.fireInterval = root.GetFloat("fireInterval", c.fireInterval);
        c.bombRadius = root.GetFloat("bombRadius", c.bombRadius);
        c.bombDamage = root.GetFloat("bombDamage", c.bombDamage);
        c.gameOverWhenAnyPlayerDies = root.GetBool("gameOverWhenAnyPlayerDies", c.gameOverWhenAnyPlayerDies);

        ProjectileSpec bulletDefaults;
        bulletDefaults.color = { 0.4f, 0.95f, 1.0f, 1.0f };
        bulletDefaults.size = { 0.7f, 0.14f, 0.14f };
        bulletDefaults.damage = 1.0f;
        bulletDefaults.radius = 0.25f;
        bulletDefaults.speed = 26.0f;
        c.playerBullet = ProjectileSpec::FromJson(root["playerBullet"], bulletDefaults);

        ProjectileSpec bombDefaults;
        bombDefaults.color = { 1.0f, 0.85f, 0.2f, 1.0f };
        bombDefaults.size = { 0.5f, 0.5f, 0.5f };
        bombDefaults.damage = 2.0f;
        bombDefaults.radius = 0.4f;
        bombDefaults.speed = 13.0f;
        c.playerBomb = ProjectileSpec::FromJson(root["playerBomb"], bombDefaults);

        for (const Json& p : root["players"].Items())
        {
            PlayerModelDef def;
            def.name = LocString::FromJson(p["name"]);
            def.model = ModelSpec::FromJson(p);
            def.speed = p.GetFloat("speed", def.speed);
            def.radius = p.GetFloat("radius", def.radius);
            def.color = ReadColor(p["color"], def.color);
            c.players.push_back(std::move(def));
        }
        if (c.players.empty())
        {
            PlayerModelDef def;
            def.name = LocString("Default");
            c.players.push_back(def);
        }
        c.sounds = ReadSounds(root["sounds"]);

        ForEachDefinition("data/enemies", [this](const Json& j, const std::string& stem)
        {
            EnemyDef def;
            def.name = ToLower(j.GetString("name", stem));
            def.model = ModelSpec::FromJson(j);
            def.life = j.GetFloat("life", def.life);
            def.radius = j.GetFloat("radius", def.radius);
            def.score = j.GetInt("score", def.score);
            def.boss = j.GetBool("boss", def.boss);
            def.contactDamage = j.GetFloat("contactDamage", def.contactDamage);
            def.motion = FunctionSpec::FromJson(j["motion"]);
            if (def.motion.Empty())
                def.motion.name = "forward";
            def.fire = FunctionSpec::FromJson(j["fire"]);
            def.projectile = ProjectileSpec::FromJson(j["projectile"], ProjectileSpec{});
            def.deathSound = j.GetString("deathSound");
            def.drop = ToLower(j.GetString("drop"));
            m_enemies[def.name] = std::move(def);
        });

        ForEachDefinition("data/items", [this](const Json& j, const std::string& stem)
        {
            ItemDef def;
            def.name = ToLower(j.GetString("name", stem));
            def.model = ModelSpec::FromJson(j);
            def.radius = j.GetFloat("radius", def.radius);
            const Json& effects = j["effects"].IsObject() ? j["effects"] : j;
            def.lives = effects.GetInt("lives", 0);
            def.health = effects.GetInt("health", 0);
            def.maxHealth = effects.GetInt("maxHealth", 0);
            def.bombs = effects.GetInt("bombs", 0);
            def.score = effects.GetInt("score", 0);
            def.motion = FunctionSpec::FromJson(j["motion"]);
            if (def.motion.Empty())
                def.motion.name = "scroll";
            def.sound = j.GetString("sound");
            def.spin = j.GetFloat("spin", def.spin);
            def.glow = ReadColor(j["glow"], def.glow);
            m_items[def.name] = std::move(def);
        });

        Log("[PlutoFall] Loaded {} enemy types, {} item types, {} player craft", m_enemies.size(), m_items.size(),
            c.players.size());
        return json.has_value();
    }

    const EnemyDef* GameData::Enemy(std::string_view name) const
    {
        auto it = m_enemies.find(ToLower(name));
        return it == m_enemies.end() ? nullptr : &it->second;
    }

    const ItemDef* GameData::Item(std::string_view name) const
    {
        auto it = m_items.find(ToLower(name));
        return it == m_items.end() ? nullptr : &it->second;
    }

    // ---------------------------------------------------------------- LevelDef

    std::optional<LevelDef> LevelDef::Load(std::string_view relativePath)
    {
        auto json = Json::Load(Paths::Asset(relativePath));
        if (!json)
            return std::nullopt;
        const Json& j = *json;
        LevelDef level;
        level.path = relativePath;
        level.name = LocString::FromJson(j["name"]);
        level.orientation = ToLower(j.GetString("orientation", "landscape")) == "portrait" ? Orientation::Portrait
                                                                                           : Orientation::Landscape;
        level.length = std::max(1.0f, j.GetFloat("length", level.length));
        level.scrollSpeed = std::max(0.1f, j.GetFloat("scrollSpeed", level.scrollSpeed));
        level.terminal = j.GetBool("terminal", false);
        level.music = j.GetString("music");
        level.backgroundColor = ReadColor(j["backgroundColor"], level.backgroundColor);
        level.backgroundImage = j.GetString("backgroundImage");
        level.backgroundParallax = j.GetFloat("backgroundParallax", level.backgroundParallax);
        level.nextDialogs = ReadStringList(j["nextDialogs"]);

        auto readSpawn = [](const Json& s, bool isItem)
        {
            SpawnDef spawn;
            spawn.isItem = isItem;
            spawn.at = ReadVec2(s["at"], { 0, 0 });
            spawn.className = ToLower(s.GetString("class", s.GetString("type")));
            spawn.position = ReadVec3(s["position"], { 0, 0, 0 });
            spawn.motion = FunctionSpec::FromJson(s["motion"]);
            spawn.fire = FunctionSpec::FromJson(s["fire"]);
            if (s.Has("boss"))
                spawn.boss = s.GetBool("boss", false);
            if (s.Has("life"))
                spawn.life = s.GetFloat("life", 1.0f);
            return spawn;
        };
        for (const Json& s : j["enemies"].Items())
            level.spawns.push_back(readSpawn(s, false));
        for (const Json& s : j["powerups"].Items())
            level.spawns.push_back(readSpawn(s, true));
        for (const Json& s : j["items"].Items())
            level.spawns.push_back(readSpawn(s, true));

        const Orientation o = level.orientation;
        std::ranges::stable_sort(level.spawns, {}, [o](const SpawnDef& s) { return s.Activation(o); });

        for (const Json& b : j["background"].Items())
        {
            BackgroundDef bg;
            bg.model = ModelSpec::FromJson(b);
            bg.position = ReadVec3(b["position"], bg.position);
            bg.motion = FunctionSpec::FromJson(b["motion"]);
            bg.spin = ReadVec3(b["spin"], bg.spin);
            level.background.push_back(std::move(bg));
        }
        return level;
    }

    // ---------------------------------------------------------------- DialogDef

    std::optional<DialogDef> DialogDef::Load(std::string_view relativePath)
    {
        auto json = Json::Load(Paths::Asset(relativePath));
        if (!json)
            return std::nullopt;
        const Json& j = *json;
        DialogDef dialog;
        dialog.path = relativePath;
        dialog.typeSound = j.GetString("typeSound");
        dialog.music = j.GetString("music");
        dialog.charactersPerSecond = std::max(1.0f, j.GetFloat("charactersPerSecond", dialog.charactersPerSecond));
        dialog.boxColor = ReadColor(j["boxColor"], dialog.boxColor);
        dialog.borderColor = ReadColor(j["borderColor"], dialog.borderColor);
        dialog.textColor = ReadColor(j["textColor"], dialog.textColor);

        const Json& sections = j.Has("sections") ? j["sections"] : j["segments"];
        for (size_t i = 0; i < sections.Size(); ++i)
        {
            const Json& s = sections[i];
            DialogSection section;
            section.id = s.GetString("id", std::format("section{}", i));
            section.background = s.GetString("background");
            section.terminal = s.GetBool("terminal", false);
            for (const Json& e : s["entries"].Items())
            {
                DialogEntry entry;
                entry.speaker = LocString::FromJson(e["speaker"]);
                entry.portrait = e.GetString("portrait");
                entry.text = LocString::FromJson(e["text"]);
                // A question may be written inline on the entry or as a nested "question" object.
                const Json& q = e["question"].IsObject() ? e["question"] : e;
                entry.key = q.GetString("key");
                if (e["question"].IsObject() && q.Has("text"))
                    entry.text = LocString::FromJson(q["text"]);
                for (const Json& r : q["responses"].Items())
                {
                    DialogResponse response;
                    response.id = r.GetString("id", r.GetString("identifier"));
                    response.text = LocString::FromJson(r["text"]);
                    response.gotoSection = r.GetString("goto");
                    entry.responses.push_back(std::move(response));
                }
                section.entries.push_back(std::move(entry));
            }
            dialog.sections.push_back(std::move(section));
        }

        const Json& next = j.Has("nextLevels") ? j["nextLevels"] : j["next-levels"];
        for (const Json& n : next.Items())
            dialog.nextLevels.push_back({ n.GetString("key"), n.GetString("level") });
        return dialog;
    }

    int DialogDef::FindSection(std::string_view id) const
    {
        for (size_t i = 0; i < sections.size(); ++i)
        {
            if (sections[i].id == id)
                return static_cast<int>(i);
        }
        return -1;
    }

    std::string SelectNextLevel(const std::map<std::string, std::string>& choices, std::span<const NextLevelOption> options)
    {
        if (options.empty())
            return {};

        auto trim = [](std::string_view s)
        {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
                s.remove_prefix(1);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
                s.remove_suffix(1);
            return s;
        };

        auto matches = [&](std::string_view key) -> bool
        {
            size_t start = 0;
            while (start <= key.size())
            {
                size_t end = key.find_first_of("&,", start);
                if (end == std::string_view::npos)
                    end = key.size();
                std::string_view condition = trim(key.substr(start, end - start));
                start = end + 1;
                if (condition.empty())
                    continue;
                const bool negate = condition.find("!=") != std::string_view::npos;
                const size_t op = negate ? condition.find("!=") : condition.find('=');
                if (op == std::string_view::npos)
                {
                    // A bare key just requires that the question was answered.
                    if (!choices.contains(std::string(condition)))
                        return false;
                    continue;
                }
                const std::string question(trim(condition.substr(0, op)));
                const std::string_view answer = trim(condition.substr(op + (negate ? 2 : 1)));
                auto it = choices.find(question);
                const bool equal = it != choices.end() && it->second == answer;
                if (equal == negate)
                    return false;
            }
            return true;
        };

        const NextLevelOption* fallback = nullptr;
        for (const NextLevelOption& option : options)
        {
            const std::string_view key = trim(option.key);
            if (key.empty() || key == "default" || key == "*")
            {
                if (!fallback)
                    fallback = &option;
                continue;
            }
            if (matches(key))
                return option.level;
        }
        return fallback ? fallback->level : options.front().level;
    }

    // ---------------------------------------------------------------- credits

    std::vector<CreditSection> LoadCredits(std::string_view relativePath)
    {
        std::vector<CreditSection> sections;
        auto json = Json::Load(Paths::Asset(relativePath));
        if (!json)
            return sections;
        const Json& list = json->IsArray() ? *json : (*json)["credits"];
        for (const Json& item : list.Items())
        {
            CreditSection section;
            section.title = LocString::FromJson(item["title"]);
            for (const Json& c : item["contributors"].Items())
            {
                CreditLine line;
                if (c.IsString())
                {
                    line.value = c.AsString();
                }
                else
                {
                    line.value = c.GetString("value");
                    line.color = ReadColor(c["color"], line.color);
                }
                section.contributors.push_back(std::move(line));
            }
            sections.push_back(std::move(section));
        }
        return sections;
    }
}
