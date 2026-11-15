#include "Input.h"
#include <Xinput.h>

namespace pf
{
    namespace
    {
        constexpr float StickDeadZone = 0.24f;
        constexpr BYTE TriggerThreshold = 40;

        void Set(DeviceState& s, Button b, bool value)
        {
            s.down[static_cast<size_t>(b)] = s.down[static_cast<size_t>(b)] || value;
        }
    }

    void Input::OnKey(WPARAM virtualKey, bool down)
    {
        if (virtualKey < m_keys.size())
            m_keys[virtualKey] = down;
        // Normalise left/right variants.
        if (virtualKey == VK_LCONTROL || virtualKey == VK_RCONTROL)
            m_keys[VK_CONTROL] = down;
        if (virtualKey == VK_LSHIFT || virtualKey == VK_RSHIFT)
            m_keys[VK_SHIFT] = down;
    }

    void Input::OnFocusLost()
    {
        m_keys.fill(false);
    }

    void Input::Update(float dt)
    {
        std::array<DeviceState, DeviceCount> previous = m_devices;

        // Keyboard.
        {
            DeviceState& s = m_devices[KeyboardDevice];
            s = DeviceState{};
            s.connected = true;
            auto key = [&](int vk) { return m_keys[static_cast<size_t>(vk)]; };
            Set(s, Button::Up, key('W') || key(VK_UP));
            Set(s, Button::Down, key('S') || key(VK_DOWN));
            Set(s, Button::Left, key('A') || key(VK_LEFT));
            Set(s, Button::Right, key('D') || key(VK_RIGHT));
            Set(s, Button::Start, key(VK_RETURN));
            Set(s, Button::Menu, key(VK_ESCAPE));
            Set(s, Button::Primary, key(VK_CONTROL));
            Set(s, Button::Secondary, key(VK_SHIFT));
            Set(s, Button::Join, key(VK_RETURN));
            Set(s, Button::Cancel, key(VK_SHIFT));
            float x = (s.Down(Button::Right) ? 1.0f : 0.0f) - (s.Down(Button::Left) ? 1.0f : 0.0f);
            float y = (s.Down(Button::Up) ? 1.0f : 0.0f) - (s.Down(Button::Down) ? 1.0f : 0.0f);
            float len = std::sqrt(x * x + y * y);
            if (len > 1.0f)
            {
                x /= len;
                y /= len;
            }
            s.move = { x, y };
        }

        // Controllers. Polling a disconnected controller is expensive, so retry those only every two seconds.
        for (DWORD pad = 0; pad < 4; ++pad)
        {
            DeviceState& s = m_devices[pad + 1];
            const bool wasConnected = s.connected;
            s = DeviceState{};
            if (!wasConnected)
            {
                m_reconnectTimer[pad] -= dt;
                if (m_reconnectTimer[pad] > 0.0f)
                    continue;
                m_reconnectTimer[pad] = 2.0f;
            }

            XINPUT_STATE state{};
            if (XInputGetState(pad, &state) != ERROR_SUCCESS)
                continue;
            s.connected = true;

            const XINPUT_GAMEPAD& g = state.Gamepad;
            float lx = std::max(-1.0f, g.sThumbLX / 32767.0f);
            float ly = std::max(-1.0f, g.sThumbLY / 32767.0f);
            float mag = std::sqrt(lx * lx + ly * ly);
            if (mag < StickDeadZone)
            {
                lx = ly = 0.0f;
            }
            else
            {
                // Rescale so movement starts from zero just outside the dead zone.
                float scaled = std::min(1.0f, (mag - StickDeadZone) / (1.0f - StickDeadZone));
                lx = lx / mag * scaled;
                ly = ly / mag * scaled;
            }

            const bool dUp = (g.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;
            const bool dDown = (g.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
            const bool dLeft = (g.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
            const bool dRight = (g.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;

            Set(s, Button::Up, dUp || ly > 0.5f);
            Set(s, Button::Down, dDown || ly < -0.5f);
            Set(s, Button::Left, dLeft || lx < -0.5f);
            Set(s, Button::Right, dRight || lx > 0.5f);
            Set(s, Button::Start, (g.wButtons & XINPUT_GAMEPAD_START) != 0);
            Set(s, Button::Menu, (g.wButtons & XINPUT_GAMEPAD_BACK) != 0);
            Set(s, Button::Primary, (g.wButtons & XINPUT_GAMEPAD_A) != 0 || g.bRightTrigger > TriggerThreshold);
            Set(s, Button::Secondary, (g.wButtons & XINPUT_GAMEPAD_B) != 0 || g.bLeftTrigger > TriggerThreshold);
            Set(s, Button::Join, (g.wButtons & XINPUT_GAMEPAD_A) != 0);
            Set(s, Button::Cancel, (g.wButtons & XINPUT_GAMEPAD_B) != 0);

            float mx = lx + (dRight ? 1.0f : 0.0f) - (dLeft ? 1.0f : 0.0f);
            float my = ly + (dUp ? 1.0f : 0.0f) - (dDown ? 1.0f : 0.0f);
            float len = std::sqrt(mx * mx + my * my);
            if (len > 1.0f)
            {
                mx /= len;
                my /= len;
            }
            s.move = { mx, my };

            // Stop rumble once its time runs out.
            if (m_rumbleTime[pad] > 0.0f)
            {
                m_rumbleTime[pad] -= dt;
                if (m_rumbleTime[pad] <= 0.0f)
                {
                    XINPUT_VIBRATION off{};
                    XInputSetState(pad, &off);
                }
            }
        }

        // Edge detection.
        for (size_t d = 0; d < m_devices.size(); ++d)
        {
            for (size_t b = 0; b < static_cast<size_t>(Button::Count); ++b)
                m_devices[d].pressed[b] = m_devices[d].down[b] && !previous[d].down[b];
        }
    }

    bool Input::AnyPressed(Button b) const
    {
        return std::ranges::any_of(m_devices, [b](const DeviceState& s) { return s.Pressed(b); });
    }

    bool Input::AnyDown(Button b) const
    {
        return std::ranges::any_of(m_devices, [b](const DeviceState& s) { return s.Down(b); });
    }

    bool Input::AnyConfirm() const
    {
        return AnyPressed(Button::Primary) || AnyPressed(Button::Join) || AnyPressed(Button::Start);
    }

    bool Input::AnyBack() const
    {
        return AnyPressed(Button::Menu) || AnyPressed(Button::Cancel);
    }

    void Input::Rumble(int device, float low, float high, float seconds)
    {
        if (device < 1 || device > 4 || !m_devices[static_cast<size_t>(device)].connected)
            return;
        const DWORD pad = static_cast<DWORD>(device - 1);
        XINPUT_VIBRATION vibration{};
        vibration.wLeftMotorSpeed = static_cast<WORD>(Clamp01(low) * 65535.0f);
        vibration.wRightMotorSpeed = static_cast<WORD>(Clamp01(high) * 65535.0f);
        XInputSetState(pad, &vibration);
        m_rumbleTime[pad] = seconds;
    }

    std::wstring Input::DeviceName(int device)
    {
        if (device == KeyboardDevice)
            return L"Keyboard";
        return std::format(L"Controller {}", device);
    }
}
