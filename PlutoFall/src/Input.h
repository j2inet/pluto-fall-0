// Input.h - unified keyboard + XInput handling.
//
// Device 0 is the keyboard, devices 1..4 are XInput controllers 0..3.
//
//  Logical button | Keyboard          | XInput controller
//  ---------------+-------------------+-------------------------------
//  Up/Down/...    | WASD / arrow keys | D-pad / left stick
//  Start          | Enter             | Start
//  Menu           | Esc               | Back (View)
//  Primary (fire) | Ctrl              | A / right trigger
//  Secondary      | Shift             | B / left trigger
//  Join (A)       | Enter             | A
//  Cancel (B)     | Shift             | B
#pragma once
#include "Common.h"

namespace pf
{
    enum class Button : int
    {
        Up,
        Down,
        Left,
        Right,
        Start,
        Menu,
        Primary,
        Secondary,
        Join,    // "A" on a controller, Enter on the keyboard
        Cancel,  // "B" on a controller, Shift on the keyboard
        Count
    };

    struct DeviceState
    {
        bool connected = false;
        std::array<bool, static_cast<size_t>(Button::Count)> down{};
        std::array<bool, static_cast<size_t>(Button::Count)> pressed{};
        XMFLOAT2 move{ 0, 0 }; // analog movement, y up, length <= 1

        bool Down(Button b) const noexcept { return down[static_cast<size_t>(b)]; }
        bool Pressed(Button b) const noexcept { return pressed[static_cast<size_t>(b)]; }
    };

    class Input
    {
    public:
        static constexpr int KeyboardDevice = 0;
        static constexpr int DeviceCount = 5;

        void OnKey(WPARAM virtualKey, bool down);
        void OnFocusLost();

        // Call once per frame before game logic.
        void Update(float dt);

        const DeviceState& Device(int index) const { return m_devices[static_cast<size_t>(index)]; }
        bool Pressed(int device, Button b) const { return Device(device).Pressed(b); }
        bool Down(int device, Button b) const { return Device(device).Down(b); }

        bool AnyPressed(Button b) const;
        bool AnyDown(Button b) const;
        // Primary, Join or Start on any device.
        bool AnyConfirm() const;
        // Menu or Cancel on any device.
        bool AnyBack() const;

        // Haptics for controllers (no-op for keyboard).
        void Rumble(int device, float low, float high, float seconds);

        static std::wstring DeviceName(int device);

    private:
        std::array<bool, 256> m_keys{};
        std::array<DeviceState, DeviceCount> m_devices{};
        std::array<float, 4> m_reconnectTimer{};
        std::array<float, 4> m_rumbleTime{};
    };
}
