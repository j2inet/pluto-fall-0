// Texture.h - WIC based image loading for Direct3D textures and Direct2D bitmaps.
// Any image that fails to load is replaced with a solid colour surface.
#pragma once
#include "Common.h"

namespace pf
{
    class Graphics;

    struct DecodedImage
    {
        UINT width = 0;
        UINT height = 0;
        std::vector<uint8_t> pixels; // tightly packed 32bpp
    };

    // Decodes a PNG/JPEG/etc. into RGBA (or premultiplied BGRA for Direct2D).
    std::optional<DecodedImage> DecodeImageFile(IWICImagingFactory* wic, const fs::path& path, bool forDirect2D);
    std::optional<DecodedImage> DecodeImageMemory(IWICImagingFactory* wic, std::span<const uint8_t> bytes, bool forDirect2D);

    // Direct3D texture cache.
    class TextureCache
    {
    public:
        void Initialize(Graphics& graphics);
        void Clear();

        // Returns nullptr if the file could not be loaded.
        ComPtr<ID3D11ShaderResourceView> TryLoadFile(const fs::path& path, bool srgb);
        ComPtr<ID3D11ShaderResourceView> TryLoadMemory(std::span<const uint8_t> bytes, bool srgb, const std::string& cacheKey);
        // 1x1 solid colour texture.
        ComPtr<ID3D11ShaderResourceView> SolidColor(const Color& color);

    private:
        ComPtr<ID3D11ShaderResourceView> CreateTexture(const DecodedImage& image, bool srgb);

        Graphics* m_graphics = nullptr;
        std::unordered_map<std::string, ComPtr<ID3D11ShaderResourceView>> m_cache;
    };

    // Direct2D bitmap cache for full screen images, portraits, etc.
    class ImageCache
    {
    public:
        void Initialize(Graphics& graphics);
        void Clear();
        // Loads an image relative to the asset folder. Never returns null: falls back to a solid colour.
        ID2D1Bitmap1* Get(std::string_view relativePath);

    private:
        Graphics* m_graphics = nullptr;
        std::unordered_map<std::string, ComPtr<ID2D1Bitmap1>> m_cache;
    };
}
