#include "Ui.h"
#include "Graphics.h"
#include "Localization.h"
#include "Util.h"

namespace pf
{
    void Ui::Initialize(Graphics& graphics)
    {
        m_graphics = &graphics;
        m_images.Initialize(graphics);
        graphics.D2D()->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &m_brush);
    }

    void Ui::ResetTextFormats()
    {
        m_formats.clear();
    }

    ID2D1SolidColorBrush* Ui::Brush(const Color& color)
    {
        m_brush->SetColor(color.ToD2D());
        return m_brush.Get();
    }

    IDWriteTextFormat* Ui::Format(const TextStyle& style)
    {
        const uint64_t key = (static_cast<uint64_t>(style.size * 10.0f) << 8) | (style.bold ? 1u : 0u) |
                             (static_cast<uint64_t>(style.align) << 1) | (style.verticalCenter ? 8u : 0u) |
                             (style.wrap ? 16u : 0u);
        if (auto it = m_formats.find(key); it != m_formats.end())
            return it->second.Get();

        ComPtr<IDWriteTextFormat> format;
        const std::wstring locale = Localization::LocaleName();
        HRESULT hr = m_graphics->DWrite()->CreateTextFormat(
            L"Segoe UI", nullptr, style.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, style.size, locale.c_str(), &format);
        if (FAILED(hr))
            return nullptr;
        switch (style.align)
        {
        case TextAlign::Left: format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING); break;
        case TextAlign::Center: format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER); break;
        case TextAlign::Right: format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING); break;
        }
        format->SetParagraphAlignment(style.verticalCenter ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER
                                                           : DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        format->SetWordWrapping(style.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        m_formats[key] = format;
        return format.Get();
    }

    void Ui::Text(std::wstring_view text, const D2D1_RECT_F& rect, const TextStyle& style)
    {
        IDWriteTextFormat* format = Format(style);
        if (!format || text.empty())
            return;
        ID2D1DeviceContext* d2d = m_graphics->D2D();
        if (style.shadow)
        {
            const float offset = std::max(2.0f, style.size * 0.06f);
            D2D1_RECT_F shadowRect{ rect.left + offset, rect.top + offset, rect.right + offset, rect.bottom + offset };
            d2d->DrawText(text.data(), static_cast<UINT32>(text.size()), format, shadowRect,
                          Brush(Color{ 0, 0, 0, style.color.a * 0.75f }), D2D1_DRAW_TEXT_OPTIONS_NONE);
        }
        d2d->DrawText(text.data(), static_cast<UINT32>(text.size()), format, rect, Brush(style.color),
                      D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    }

    void Ui::Text(std::string_view utf8, const D2D1_RECT_F& rect, const TextStyle& style)
    {
        Text(std::wstring_view(Widen(utf8)), rect, style);
    }

    D2D1_SIZE_F Ui::Measure(std::wstring_view text, float maxWidth, const TextStyle& style)
    {
        IDWriteTextFormat* format = Format(style);
        if (!format)
            return { 0, 0 };
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(m_graphics->DWrite()->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format,
                                                          maxWidth, 10000.0f, &layout)))
            return { 0, 0 };
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        return { metrics.widthIncludingTrailingWhitespace, metrics.height };
    }

    void Ui::FillRect(const D2D1_RECT_F& rect, const Color& color)
    {
        m_graphics->D2D()->FillRectangle(rect, Brush(color));
    }

    void Ui::FillRoundRect(const D2D1_RECT_F& rect, float radius, const Color& color)
    {
        m_graphics->D2D()->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), Brush(color));
    }

    void Ui::DrawRect(const D2D1_RECT_F& rect, const Color& color, float thickness)
    {
        m_graphics->D2D()->DrawRectangle(rect, Brush(color), thickness);
    }

    void Ui::DrawRoundRect(const D2D1_RECT_F& rect, float radius, const Color& color, float thickness)
    {
        m_graphics->D2D()->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), Brush(color), thickness);
    }

    void Ui::Line(D2D1_POINT_2F a, D2D1_POINT_2F b, const Color& color, float thickness)
    {
        m_graphics->D2D()->DrawLine(a, b, Brush(color), thickness);
    }

    void Ui::FillScreen(const Color& color)
    {
        FillRect(D2D1::RectF(0, 0, Graphics::CanvasWidth, Graphics::CanvasHeight), color);
    }

    void Ui::Image(std::string_view relativePath, const D2D1_RECT_F& rect, float opacity, bool cover)
    {
        Image(m_images.Get(relativePath), rect, opacity, cover);
    }

    void Ui::Image(ID2D1Bitmap* bitmap, const D2D1_RECT_F& rect, float opacity, bool cover)
    {
        if (!bitmap)
            return;
        const D2D1_SIZE_F size = bitmap->GetSize();
        D2D1_RECT_F source = D2D1::RectF(0, 0, size.width, size.height);
        if (cover && size.width > 1.0f && size.height > 1.0f)
        {
            const float rectAspect = (rect.right - rect.left) / std::max(1.0f, rect.bottom - rect.top);
            const float imageAspect = size.width / size.height;
            if (imageAspect > rectAspect)
            {
                const float w = size.height * rectAspect;
                source.left = (size.width - w) * 0.5f;
                source.right = source.left + w;
            }
            else
            {
                const float h = size.width / rectAspect;
                source.top = (size.height - h) * 0.5f;
                source.bottom = source.top + h;
            }
        }
        m_graphics->D2D()->DrawBitmap(bitmap, &rect, opacity, D2D1_INTERPOLATION_MODE_LINEAR, &source, nullptr);
    }

    void Ui::Meter(const D2D1_RECT_F& rect, float fraction, const Color& fill, const Color& back)
    {
        FillRect(rect, back);
        D2D1_RECT_F filled = rect;
        filled.right = rect.left + (rect.right - rect.left) * Clamp01(fraction);
        FillRect(filled, fill);
        DrawRect(rect, Color{ 1, 1, 1, 0.6f }, 1.5f);
    }
}
