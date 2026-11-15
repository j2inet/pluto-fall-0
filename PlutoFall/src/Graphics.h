// Graphics.h - Direct3D 11 device and swap chain with a Direct2D/DirectWrite overlay.
//
// All 2D drawing uses a virtual 1920x1080 canvas that is letterboxed into the window,
// and the 3D viewport covers the same 16:9 area.
#pragma once
#include "Common.h"

namespace pf
{
    class Graphics
    {
    public:
        static constexpr float CanvasWidth = 1920.0f;
        static constexpr float CanvasHeight = 1080.0f;

        bool Initialize(HWND hwnd);
        void Resize(UINT width, UINT height);

        // Clears the whole back buffer and depth buffer and binds them.
        void BeginFrame(const Color& clear);
        void Present();
        void ClearDepth();

        // Direct2D drawing. Calls may be interleaved with 3D rendering.
        void Begin2D();
        void End2D();

        ID3D11Device* Device() const noexcept { return m_device.Get(); }
        ID3D11DeviceContext* Context() const noexcept { return m_context.Get(); }
        ID2D1DeviceContext* D2D() const noexcept { return m_d2dContext.Get(); }
        IDWriteFactory* DWrite() const noexcept { return m_dwrite.Get(); }
        IWICImagingFactory* Wic() const noexcept { return m_wic.Get(); }

        const D3D11_VIEWPORT& Viewport() const noexcept { return m_viewport; }
        UINT Width() const noexcept { return m_width; }
        UINT Height() const noexcept { return m_height; }
        bool IsMinimized() const noexcept { return m_width == 0 || m_height == 0; }

        // Binds the back buffer + depth buffer and the letterboxed viewport.
        void BindDefaultTarget();

    private:
        bool CreateDevice();
        void CreateSizeDependentResources();
        void ReleaseSizeDependentResources();

        HWND m_hwnd = nullptr;
        UINT m_width = 0;
        UINT m_height = 0;
        D3D11_VIEWPORT m_viewport{};

        ComPtr<ID3D11Device> m_device;
        ComPtr<ID3D11DeviceContext> m_context;
        ComPtr<IDXGISwapChain1> m_swapChain;
        ComPtr<ID3D11RenderTargetView> m_rtv;
        ComPtr<ID3D11DepthStencilView> m_dsv;

        ComPtr<ID2D1Factory1> m_d2dFactory;
        ComPtr<ID2D1Device> m_d2dDevice;
        ComPtr<ID2D1DeviceContext> m_d2dContext;
        ComPtr<ID2D1Bitmap1> m_d2dTarget;
        ComPtr<IDWriteFactory> m_dwrite;
        ComPtr<IWICImagingFactory> m_wic;
        bool m_in2D = false;
    };
}
