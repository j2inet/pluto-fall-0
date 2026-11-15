// Common.h - shared includes and small helpers used throughout Pluto Fall.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <wrl/client.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <DirectXMath.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pf
{
    using Microsoft::WRL::ComPtr;
    namespace fs = std::filesystem;
    using namespace DirectX;

    constexpr float Pi = std::numbers::pi_v<float>;

    // Orientation of a level. Portrait scrolls top->bottom, landscape scrolls right->left.
    enum class Orientation
    {
        Portrait,
        Landscape
    };

    // Simple linear RGBA colour.
    struct Color
    {
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;

        constexpr Color WithAlpha(float alpha) const noexcept { return { r, g, b, alpha }; }
        XMFLOAT4 ToFloat4() const noexcept { return { r, g, b, a }; }
        D2D1_COLOR_F ToD2D() const noexcept { return D2D1_COLOR_F{ r, g, b, a }; }
    };

    namespace Colors
    {
        constexpr Color White{ 1, 1, 1, 1 };
        constexpr Color Black{ 0, 0, 0, 1 };
        constexpr Color Gold{ 1.0f, 0.82f, 0.25f, 1 };
        constexpr Color Grey{ 0.6f, 0.6f, 0.65f, 1 };
        constexpr Color Cyan{ 0.3f, 0.9f, 1.0f, 1 };
        constexpr Color Red{ 1.0f, 0.25f, 0.2f, 1 };
        constexpr Color Green{ 0.3f, 1.0f, 0.4f, 1 };
    }

    inline float Clamp01(float v) noexcept { return std::clamp(v, 0.0f, 1.0f); }
    inline float Lerp(float a, float b, float t) noexcept { return a + (b - a) * t; }
}
