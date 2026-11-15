// Ui.h - Direct2D/DirectWrite helpers. All coordinates are in the 1920x1080 virtual canvas.
#pragma once
#include "Common.h"
#include "Texture.h"

namespace pf
{
    class Graphics;

    enum class TextAlign
    {
        Left,
        Center,
        Right
    };

    struct TextStyle
    {
        float size = 32.0f;
        Color color = Colors::White;
        TextAlign align = TextAlign::Left;
        bool bold = false;
        bool verticalCenter = false;
        bool wrap = true;
        bool shadow = false;
    };

    class Ui
    {
    public:
        void Initialize(Graphics& graphics);
        // Call when the language changes (text formats are created per locale).
        void ResetTextFormats();

        void Text(std::wstring_view text, const D2D1_RECT_F& rect, const TextStyle& style);
        void Text(std::string_view utf8, const D2D1_RECT_F& rect, const TextStyle& style);
        D2D1_SIZE_F Measure(std::wstring_view text, float maxWidth, const TextStyle& style);

        void FillRect(const D2D1_RECT_F& rect, const Color& color);
        void FillRoundRect(const D2D1_RECT_F& rect, float radius, const Color& color);
        void DrawRect(const D2D1_RECT_F& rect, const Color& color, float thickness = 2.0f);
        void DrawRoundRect(const D2D1_RECT_F& rect, float radius, const Color& color, float thickness = 2.0f);
        void Line(D2D1_POINT_2F a, D2D1_POINT_2F b, const Color& color, float thickness = 2.0f);
        void FillScreen(const Color& color);

        // Draws an image. "cover" scales preserving aspect ratio to fill the rect (cropping);
        // otherwise the image is stretched.
        void Image(std::string_view relativePath, const D2D1_RECT_F& rect, float opacity = 1.0f, bool cover = false);
        void Image(ID2D1Bitmap* bitmap, const D2D1_RECT_F& rect, float opacity = 1.0f, bool cover = false);
        ImageCache& Images() noexcept { return m_images; }

        // A bar for health/volume.
        void Meter(const D2D1_RECT_F& rect, float fraction, const Color& fill, const Color& back);

    private:
        IDWriteTextFormat* Format(const TextStyle& style);
        ID2D1SolidColorBrush* Brush(const Color& color);

        Graphics* m_graphics = nullptr;
        ComPtr<ID2D1SolidColorBrush> m_brush;
        std::unordered_map<uint64_t, ComPtr<IDWriteTextFormat>> m_formats;
        ImageCache m_images;
    };
}
