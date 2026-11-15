#include "MotionFunctions.h"
#include "Util.h"

namespace pf
{
    Axes Axes::For(Orientation orientation)
    {
        if (orientation == Orientation::Landscape)
            return { { -1.0f, 0.0f }, { 0.0f, 1.0f } };
        return { { 0.0f, -1.0f }, { 1.0f, 0.0f } };
    }

    namespace
    {
        std::unordered_map<std::string, MotionRegistry::Factory>& Registry()
        {
            static std::unordered_map<std::string, MotionRegistry::Factory> registry;
            return registry;
        }

        class ForwardMotion final : public MotionFunction
        {
        public:
            std::string_view Name() const override { return "forward"; }
            void Configure(const Json& p) override { m_speed = p.GetFloat("speed", m_speed); }
            XMFLOAT3 Evaluate(float t, Orientation o) const override
            {
                return Offset(m_start, Axes::For(o), m_speed * t, 0.0f);
            }

        private:
            float m_speed = 4.0f;
        };

        class WaveMotion final : public MotionFunction
        {
        public:
            std::string_view Name() const override { return "wave"; }
            void Configure(const Json& p) override
            {
                m_speed = p.GetFloat("speed", m_speed);
                m_amplitude = p.GetFloat("amplitude", m_amplitude);
                m_frequency = p.GetFloat("frequency", m_frequency);
                m_phase = p.GetFloat("phase", m_phase);
            }
            XMFLOAT3 Evaluate(float t, Orientation o) const override
            {
                const float across = m_amplitude * std::sin(2.0f * Pi * m_frequency * t + m_phase);
                return Offset(m_start, Axes::For(o), m_speed * t, across);
            }

        private:
            float m_speed = 3.5f;
            float m_amplitude = 2.0f;
            float m_frequency = 0.4f;
            float m_phase = 0.0f;
        };

        class HoldMotion final : public MotionFunction
        {
        public:
            std::string_view Name() const override { return "hold"; }
            void Configure(const Json& p) override
            {
                if (const Json& target = p["target"]; target.Size() >= 2)
                    m_target = XMFLOAT2{ target[0].AsFloat(), target[1].AsFloat() };
                m_distance = p.GetFloat("distance", m_distance);
                m_speed = std::max(0.1f, p.GetFloat("speed", m_speed));
                m_holdTime = p.GetFloat("holdTime", m_holdTime);
                m_exitSpeed = p.GetFloat("exitSpeed", m_exitSpeed);
            }
            XMFLOAT3 Evaluate(float t, Orientation o) const override
            {
                const Axes axes = Axes::For(o);
                XMFLOAT3 target = m_target ? XMFLOAT3{ m_target->x, m_target->y, m_start.z }
                                           : Offset(m_start, axes, m_distance, 0.0f);
                const float dx = target.x - m_start.x, dy = target.y - m_start.y;
                const float travel = std::sqrt(dx * dx + dy * dy);
                const float arrive = travel / m_speed;
                if (t < arrive)
                {
                    // Ease out so the object glides into place.
                    const float s = t / arrive;
                    const float eased = 1.0f - (1.0f - s) * (1.0f - s);
                    return { m_start.x + dx * eased, m_start.y + dy * eased, m_start.z };
                }
                if (m_holdTime > 0.0f && t > arrive + m_holdTime)
                {
                    const float leaving = t - arrive - m_holdTime;
                    return Offset(target, axes, m_exitSpeed * leaving * leaving * 0.5f + m_exitSpeed * leaving * 0.5f, 0.0f);
                }
                return target;
            }

        private:
            std::optional<XMFLOAT2> m_target;
            float m_distance = 6.0f;
            float m_speed = 6.0f;
            float m_holdTime = 0.0f; // 0 = hold forever
            float m_exitSpeed = 6.0f;
        };

        class ScrollMotion final : public MotionFunction
        {
        public:
            std::string_view Name() const override { return "scroll"; }
            void Configure(const Json& p) override { m_factor = p.GetFloat("factor", m_factor); }
            XMFLOAT3 Evaluate(float t, Orientation o) const override
            {
                return Offset(m_start, Axes::For(o), m_scrollSpeed * m_factor * t, 0.0f);
            }

        private:
            float m_factor = 1.0f;
        };

        class StaticMotion final : public MotionFunction
        {
        public:
            std::string_view Name() const override { return "static"; }
            XMFLOAT3 Evaluate(float, Orientation) const override { return m_start; }
        };

        class CircleMotion final : public MotionFunction
        {
        public:
            std::string_view Name() const override { return "circle"; }
            void Configure(const Json& p) override
            {
                m_radius = p.GetFloat("radius", m_radius);
                m_angularSpeed = p.GetFloat("angularSpeed", m_angularSpeed);
                m_speed = p.GetFloat("speed", m_speed);
            }
            XMFLOAT3 Evaluate(float t, Orientation o) const override
            {
                const float angle = m_angularSpeed * t;
                // Starts on the circle at angle 0 so there is no jump at t = 0.
                return Offset(m_start, Axes::For(o), m_speed * t + m_radius * std::sin(angle),
                              m_radius * (std::cos(angle) - 1.0f));
            }

        private:
            float m_radius = 2.0f;
            float m_angularSpeed = 2.0f;
            float m_speed = 2.5f;
        };

        class DiveMotion final : public MotionFunction
        {
        public:
            std::string_view Name() const override { return "dive"; }
            void Configure(const Json& p) override
            {
                m_delay = p.GetFloat("delay", m_delay);
                m_speed = p.GetFloat("speed", m_speed);
                m_entrySpeed = std::max(0.1f, p.GetFloat("entrySpeed", m_entrySpeed));
                m_distance = p.GetFloat("distance", m_distance);
            }
            XMFLOAT3 Evaluate(float t, Orientation o) const override
            {
                const Axes axes = Axes::For(o);
                const float entryTime = m_distance / m_entrySpeed;
                if (t < entryTime)
                    return Offset(m_start, axes, m_entrySpeed * t, 0.0f);
                if (t < entryTime + m_delay)
                    return Offset(m_start, axes, m_distance, 0.0f);
                const float d = t - entryTime - m_delay;
                return Offset(m_start, axes, m_distance + m_speed * d * d * 0.8f + m_speed * d * 0.2f, 0.0f);
            }

        private:
            float m_delay = 1.0f;
            float m_speed = 14.0f;
            float m_entrySpeed = 6.0f;
            float m_distance = 4.0f;
        };

        template <class T>
        MotionRegistry::Factory Make()
        {
            return [] { return std::make_unique<T>(); };
        }
    }

    void MotionRegistry::Register(std::string name, Factory factory)
    {
        Registry()[ToLower(name)] = std::move(factory);
    }

    void MotionRegistry::RegisterBuiltIns()
    {
        Register("forward", Make<ForwardMotion>());
        Register("wave", Make<WaveMotion>());
        Register("hold", Make<HoldMotion>());
        Register("scroll", Make<ScrollMotion>());
        Register("static", Make<StaticMotion>());
        Register("circle", Make<CircleMotion>());
        Register("dive", Make<DiveMotion>());
    }

    std::unique_ptr<MotionFunction> MotionRegistry::Create(std::string_view name, const Json& params)
    {
        auto& registry = Registry();
        auto it = registry.find(ToLower(name));
        if (it == registry.end())
        {
            if (!name.empty())
                Log("[PlutoFall] Unknown motion function '{}', using 'forward'", name);
            it = registry.find("forward");
            if (it == registry.end())
                return nullptr;
        }
        auto motion = it->second();
        motion->Configure(params);
        return motion;
    }
}
