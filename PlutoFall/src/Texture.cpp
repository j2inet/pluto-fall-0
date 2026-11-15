#include "Texture.h"
#include "Graphics.h"
#include "Util.h"

namespace pf
{
    namespace
    {
        constexpr UINT MaxTextureSize = 4096;

        std::optional<DecodedImage> DecodeFrame(IWICImagingFactory* wic, IWICBitmapDecoder* decoder, bool forDirect2D)
        {
            ComPtr<IWICBitmapFrameDecode> frame;
            if (FAILED(decoder->GetFrame(0, &frame)))
                return std::nullopt;

            ComPtr<IWICBitmapSource> source = frame;
            UINT width = 0, height = 0;
            frame->GetSize(&width, &height);
            if (width == 0 || height == 0)
                return std::nullopt;

            // Clamp huge images.
            if (width > MaxTextureSize || height > MaxTextureSize)
            {
                const float scale = static_cast<float>(MaxTextureSize) / static_cast<float>(std::max(width, height));
                const UINT newWidth = std::max(1u, static_cast<UINT>(width * scale));
                const UINT newHeight = std::max(1u, static_cast<UINT>(height * scale));
                ComPtr<IWICBitmapScaler> scaler;
                if (SUCCEEDED(wic->CreateBitmapScaler(&scaler)) &&
                    SUCCEEDED(scaler->Initialize(frame.Get(), newWidth, newHeight, WICBitmapInterpolationModeFant)))
                {
                    source = scaler;
                    width = newWidth;
                    height = newHeight;
                }
            }

            ComPtr<IWICFormatConverter> converter;
            if (FAILED(wic->CreateFormatConverter(&converter)))
                return std::nullopt;
            const WICPixelFormatGUID& target = forDirect2D ? GUID_WICPixelFormat32bppPBGRA : GUID_WICPixelFormat32bppRGBA;
            if (FAILED(converter->Initialize(source.Get(), target, WICBitmapDitherTypeNone, nullptr, 0.0,
                                             WICBitmapPaletteTypeCustom)))
                return std::nullopt;

            DecodedImage image;
            image.width = width;
            image.height = height;
            image.pixels.resize(static_cast<size_t>(width) * height * 4);
            if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(image.pixels.size()), image.pixels.data())))
                return std::nullopt;
            return image;
        }
    }

    std::optional<DecodedImage> DecodeImageFile(IWICImagingFactory* wic, const fs::path& path, bool forDirect2D)
    {
        if (!wic || path.empty())
            return std::nullopt;
        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
                                                  &decoder)))
            return std::nullopt;
        return DecodeFrame(wic, decoder.Get(), forDirect2D);
    }

    std::optional<DecodedImage> DecodeImageMemory(IWICImagingFactory* wic, std::span<const uint8_t> bytes, bool forDirect2D)
    {
        if (!wic || bytes.empty())
            return std::nullopt;
        ComPtr<IWICStream> stream;
        if (FAILED(wic->CreateStream(&stream)))
            return std::nullopt;
        if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size()))))
            return std::nullopt;
        ComPtr<IWICBitmapDecoder> decoder;
        if (FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)))
            return std::nullopt;
        return DecodeFrame(wic, decoder.Get(), forDirect2D);
    }

    // ---------------------------------------------------------------- TextureCache

    void TextureCache::Initialize(Graphics& graphics)
    {
        m_graphics = &graphics;
    }

    void TextureCache::Clear()
    {
        m_cache.clear();
    }

    ComPtr<ID3D11ShaderResourceView> TextureCache::CreateTexture(const DecodedImage& image, bool srgb)
    {
        ID3D11Device* device = m_graphics->Device();
        ID3D11DeviceContext* context = m_graphics->Context();

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = image.width;
        desc.Height = image.height;
        desc.MipLevels = 0; // full chain
        desc.ArraySize = 1;
        desc.Format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

        ComPtr<ID3D11Texture2D> texture;
        if (!Check(device->CreateTexture2D(&desc, nullptr, &texture), "CreateTexture2D"))
            return nullptr;
        context->UpdateSubresource(texture.Get(), 0, nullptr, image.pixels.data(), image.width * 4, 0);

        ComPtr<ID3D11ShaderResourceView> srv;
        if (!Check(device->CreateShaderResourceView(texture.Get(), nullptr, &srv), "CreateShaderResourceView"))
            return nullptr;
        context->GenerateMips(srv.Get());
        return srv;
    }

    ComPtr<ID3D11ShaderResourceView> TextureCache::TryLoadFile(const fs::path& path, bool srgb)
    {
        const std::string key = Narrow(path.wstring()) + (srgb ? "|srgb" : "|linear");
        if (auto it = m_cache.find(key); it != m_cache.end())
            return it->second;
        std::error_code ec;
        ComPtr<ID3D11ShaderResourceView> srv;
        if (fs::exists(path, ec))
        {
            if (auto image = DecodeImageFile(m_graphics->Wic(), path, false))
                srv = CreateTexture(*image, srgb);
        }
        if (srv)
            m_cache[key] = srv;
        return srv;
    }

    ComPtr<ID3D11ShaderResourceView> TextureCache::TryLoadMemory(std::span<const uint8_t> bytes, bool srgb,
                                                                 const std::string& cacheKey)
    {
        const std::string key = cacheKey + (srgb ? "|srgb" : "|linear");
        if (auto it = m_cache.find(key); it != m_cache.end())
            return it->second;
        ComPtr<ID3D11ShaderResourceView> srv;
        if (auto image = DecodeImageMemory(m_graphics->Wic(), bytes, false))
            srv = CreateTexture(*image, srgb);
        if (srv)
            m_cache[key] = srv;
        return srv;
    }

    ComPtr<ID3D11ShaderResourceView> TextureCache::SolidColor(const Color& color)
    {
        const auto toByte = [](float v) { return static_cast<uint8_t>(Clamp01(v) * 255.0f + 0.5f); };
        const std::string key = std::format("solid:{:02x}{:02x}{:02x}{:02x}", toByte(color.r), toByte(color.g),
                                            toByte(color.b), toByte(color.a));
        if (auto it = m_cache.find(key); it != m_cache.end())
            return it->second;

        const uint8_t pixel[4] = { toByte(color.r), toByte(color.g), toByte(color.b), toByte(color.a) };
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = 1;
        desc.Height = 1;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{ pixel, 4, 4 };
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> srv;
        if (SUCCEEDED(m_graphics->Device()->CreateTexture2D(&desc, &data, &texture)))
            m_graphics->Device()->CreateShaderResourceView(texture.Get(), nullptr, &srv);
        m_cache[key] = srv;
        return srv;
    }

    // ---------------------------------------------------------------- ImageCache

    void ImageCache::Initialize(Graphics& graphics)
    {
        m_graphics = &graphics;
    }

    void ImageCache::Clear()
    {
        m_cache.clear();
    }

    ID2D1Bitmap1* ImageCache::Get(std::string_view relativePath)
    {
        std::string key(relativePath);
        if (auto it = m_cache.find(key); it != m_cache.end())
            return it->second.Get();

        ID2D1DeviceContext* d2d = m_graphics->D2D();
        D2D1_BITMAP_PROPERTIES1 props{};
        props.pixelFormat = { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED };
        props.dpiX = 96.0f;
        props.dpiY = 96.0f;

        ComPtr<ID2D1Bitmap1> bitmap;
        if (!relativePath.empty())
        {
            if (auto image = DecodeImageFile(m_graphics->Wic(), Paths::Asset(relativePath), true))
            {
                d2d->CreateBitmap(D2D1::SizeU(image->width, image->height), image->pixels.data(), image->width * 4,
                                  &props, &bitmap);
            }
        }
        if (!bitmap)
        {
            // Solid colour surface derived from the file name so it is stable between runs.
            if (!relativePath.empty())
                Log("[PlutoFall] Could not load image '{}'; using a solid colour", key);
            Color c = ColorFromHash(HashString(key));
            c.r *= 0.5f;
            c.g *= 0.5f;
            c.b *= 0.5f;
            const uint8_t pixel[4] = { static_cast<uint8_t>(c.b * 255), static_cast<uint8_t>(c.g * 255),
                                       static_cast<uint8_t>(c.r * 255), 255 };
            d2d->CreateBitmap(D2D1::SizeU(1, 1), pixel, 4, &props, &bitmap);
        }
        m_cache[key] = bitmap;
        return bitmap.Get();
    }
}
