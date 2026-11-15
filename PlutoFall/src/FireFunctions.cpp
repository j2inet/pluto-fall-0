#include "FireFunctions.h"
#include "MotionFunctions.h"
#include "Util.h"

namespace pf
{
    void FireFunction::Configure(const Json& p)
    {
        m_interval = std::max(0.05f, p.GetFloat("interval", m_interval));
        m_delay = p.GetFloat("delay", m_delay);
        m_speed = p.GetFloat("speed", m_speed);
        m_count = std::max(1, p.GetInt("count", m_count));
        m_spreadDegrees = p.GetFloat("spread", m_spreadDegrees);
    }

    bool FireFunction::Due(float time)
    {
        if (m_nextShot < 0.0f)
            m_nextShot = m_delay;
        if (time < m_nextShot)
            return false;
        m_nextShot += m_interval;
        if (m_nextShot < time) // don't fire a burst of catch-up shots after a hitch
            m_nextShot = time + m_interval;
        return true;
    }

    Volley FireFunction::Fan(XMFLOAT3 origin, XMFLOAT2 direction) const
    {
        Volley volley;
        const float baseAngle = std::atan2(direction.y, direction.x);
        const float spread = XMConvertToRadians(m_spreadDegrees);
        for (int i = 0; i < m_count; ++i)
        {
            const float offset = m_count == 1 ? 0.0f : spread * (static_cast<float>(i) / (m_count - 1) - 0.5f) * 2.0f;
            const float angle = baseAngle + offset;
            volley.push_back({ origin, { std::cos(angle) * m_speed, std::sin(angle) * m_speed, 0.0f } });
        }
        return volley;
    }

    namespace
    {
        std::unordered_map<std::string, FireRegistry::Factory>& Registry()
        {
            static std::unordered_map<std::string, FireRegistry::Factory> registry;
            return registry;
        }

        class NoFire final : public FireFunction
        {
        public:
            std::string_view Name() const override { return "none"; }
            std::optional<Volley> Evaluate(float, XMFLOAT3, std::span<const XMFLOAT3>, Orientation) override
            {
                return std::nullopt;
            }
        };

        class StraightAheadFire final : public FireFunction
        {
        public:
            std::string_view Name() const override { return "straight-ahead"; }
            std::optional<Volley> Evaluate(float time, XMFLOAT3 origin, std::span<const XMFLOAT3>, Orientation o) override
            {
                if (!Due(time))
                    return std::nullopt;
                return Fan(origin, Axes::For(o).forward);
            }
        };

        class HomedFire final : public FireFunction
        {
        public:
            std::string_view Name() const override { return "homed"; }
            std::optional<Volley> Evaluate(float time, XMFLOAT3 origin, std::span<const XMFLOAT3> players,
                                           Orientation o) override
            {
                if (!Due(time))
                    return std::nullopt;
                XMFLOAT2 direction = Axes::For(o).forward;
                float best = FLT_MAX;
                for (const XMFLOAT3& p : players)
                {
                    const float dx = p.x - origin.x, dy = p.y - origin.y;
                    const float d2 = dx * dx + dy * dy;
                    if (d2 < best && d2 > 0.0001f)
                    {
                        best = d2;
                        const float len = std::sqrt(d2);
                        direction = { dx / len, dy / len };
                    }
                }
                return Fan(origin, direction);
            }
        };

        class RadialFire final : public FireFunction
        {
        public:
            RadialFire() { m_count = 12; }
            std::string_view Name() const override { return "radial"; }
            std::optional<Volley> Evaluate(float time, XMFLOAT3 origin, std::span<const XMFLOAT3>, Orientation) override
            {
                if (!Due(time))
                    return std::nullopt;
                Volley volley;
                const float jitter = RandomFloat(0.0f, 2.0f * Pi);
                for (int i = 0; i < m_count; ++i)
                {
                    const float angle = jitter + 2.0f * Pi * static_cast<float>(i) / static_cast<float>(m_count);
                    volley.push_back({ origin, { std::cos(angle) * m_speed, std::sin(angle) * m_speed, 0.0f } });
                }
                return volley;
            }
        };

        class SpiralFire final : public FireFunction
        {
        public:
            SpiralFire()
            {
                m_interval = 0.12f;
                m_count = 2;
            }
            std::string_view Name() const override { return "spiral"; }
            void Configure(const Json& p) override
            {
                FireFunction::Configure(p);
                m_rotation = XMConvertToRadians(p.GetFloat("rotation", XMConvertToDegrees(m_rotation)));
            }
            std::optional<Volley> Evaluate(float time, XMFLOAT3 origin, std::span<const XMFLOAT3>, Orientation) override
            {
                if (!Due(time))
                    return std::nullopt;
                Volley volley;
                m_angle += m_rotation;
                for (int i = 0; i < m_count; ++i)
                {
                    const float angle = m_angle + 2.0f * Pi * static_cast<float>(i) / static_cast<float>(m_count);
                    volley.push_back({ origin, { std::cos(angle) * m_speed, std::sin(angle) * m_speed, 0.0f } });
                }
                return volley;
            }

        private:
            float m_rotation = XMConvertToRadians(17.0f);
            float m_angle = 0.0f;
        };

        template <class T>
        FireRegistry::Factory Make()
        {
            return [] { return std::make_unique<T>(); };
        }
    }

    void FireRegistry::Register(std::string name, Factory factory)
    {
        Registry()[ToLower(name)] = std::move(factory);
    }

    void FireRegistry::RegisterBuiltIns()
    {
        Register("none", Make<NoFire>());
        Register("straight-ahead", Make<StraightAheadFire>());
        Register("homed", Make<HomedFire>());
        Register("radial", Make<RadialFire>());
        Register("spiral", Make<SpiralFire>());
    }

    std::unique_ptr<FireFunction> FireRegistry::Create(std::string_view name, const Json& params)
    {
        auto& registry = Registry();
        auto it = registry.find(ToLower(name));
        if (it == registry.end())
        {
            if (!name.empty())
                Log("[PlutoFall] Unknown fire function '{}', using 'none'", name);
            it = registry.find("none");
            if (it == registry.end())
                return nullptr;
        }
        auto fire = it->second();
        fire->Configure(params);
        return fire;
    }
}
