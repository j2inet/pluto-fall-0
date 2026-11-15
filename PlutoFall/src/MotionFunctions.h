// MotionFunctions.h - named, data driven movement patterns.
//
// A motion function is an object exposing Evaluate(time, orientation) -> (x, y, z). The hosting
// object holds any settings (speed, amplitude, ...) read from JSON. Each function is written in
// terms of the "forward" axis (the direction the playfield scrolls: left in landscape, down in
// portrait) and the "across" axis, so the same function works for both orientations.
//
// Built in names:
//   "forward" - moves in the scroll direction.             params: speed
//   "wave"    - moves forward along a sine wave.           params: speed, amplitude, frequency, phase
//   "hold"    - moves to a position and holds it there.    params: target [x,y] or distance, speed, holdTime, exitSpeed
//   "scroll"  - moves with the scrolling background.       params: factor
//   "static"  - stays at the start position.
//   "circle"  - circles around a point drifting forward.   params: radius, angularSpeed, speed
//   "dive"    - holds briefly, then dives forward quickly. params: delay, speed, entrySpeed, distance
#pragma once
#include "Common.h"
#include "Json.h"

namespace pf
{
    struct Axes
    {
        XMFLOAT2 forward; // direction the playfield content moves
        XMFLOAT2 across;  // perpendicular axis

        static Axes For(Orientation orientation);
    };

    class MotionFunction
    {
    public:
        virtual ~MotionFunction() = default;
        virtual std::string_view Name() const = 0;

        // Reads optional parameters from JSON (unknown keys are ignored).
        virtual void Configure(const Json& params) { (void)params; }

        // Sets the starting position (in playfield coordinates) and the level scroll speed.
        void Begin(XMFLOAT3 start, float scrollSpeed)
        {
            m_start = start;
            m_scrollSpeed = scrollSpeed;
        }

        // time: seconds since the object became active.
        virtual XMFLOAT3 Evaluate(float time, Orientation orientation) const = 0;

    protected:
        static XMFLOAT3 Offset(XMFLOAT3 start, const Axes& axes, float along, float across)
        {
            return { start.x + axes.forward.x * along + axes.across.x * across,
                     start.y + axes.forward.y * along + axes.across.y * across, start.z };
        }

        XMFLOAT3 m_start{ 0, 0, 0 };
        float m_scrollSpeed = 0.0f;
    };

    class MotionRegistry
    {
    public:
        using Factory = std::function<std::unique_ptr<MotionFunction>()>;

        static void Register(std::string name, Factory factory);
        // Creates and configures a motion function. Unknown names fall back to "forward".
        static std::unique_ptr<MotionFunction> Create(std::string_view name, const Json& params);
        static void RegisterBuiltIns();
    };
}
