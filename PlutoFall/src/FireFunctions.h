// FireFunctions.h - named, data driven enemy weapon patterns.
//
// A fire function decides when an enemy fires. Evaluate() receives the enemy's age (game time
// since it became active), the enemy position and the positions of all living players. It returns
// std::nullopt when it is not time to fire, or a volley of projectile spawn descriptions.
//
// Built in names (all accept: interval, delay, speed, count, spread):
//   "none"           - never fires.
//   "straight-ahead" - fires in the direction the playfield scrolls.
//   "homed"          - fires toward the nearest player.
//   "radial"         - fires a ring of projectiles in all directions.
//   "spiral"         - fires a rotating stream.        extra: rotation (degrees per shot)
#pragma once
#include "Common.h"
#include "Json.h"

namespace pf
{
    struct ProjectileSpawn
    {
        XMFLOAT3 position;
        XMFLOAT3 velocity;
    };

    using Volley = std::vector<ProjectileSpawn>;

    class FireFunction
    {
    public:
        virtual ~FireFunction() = default;
        virtual std::string_view Name() const = 0;
        virtual void Configure(const Json& params);

        virtual std::optional<Volley> Evaluate(float time, XMFLOAT3 origin, std::span<const XMFLOAT3> players,
                                               Orientation orientation) = 0;

    protected:
        // Returns true (and schedules the next shot) when a shot is due.
        bool Due(float time);
        // Builds a fan of `count` shots centred on the given direction.
        Volley Fan(XMFLOAT3 origin, XMFLOAT2 direction) const;

        float m_interval = 1.6f;
        float m_delay = 0.8f;
        float m_speed = 8.0f;
        int m_count = 1;
        float m_spreadDegrees = 15.0f;
        float m_nextShot = -1.0f;
    };

    class FireRegistry
    {
    public:
        using Factory = std::function<std::unique_ptr<FireFunction>()>;
        static void Register(std::string name, Factory factory);
        // Unknown or empty names produce "none".
        static std::unique_ptr<FireFunction> Create(std::string_view name, const Json& params);
        static void RegisterBuiltIns();
    };
}
