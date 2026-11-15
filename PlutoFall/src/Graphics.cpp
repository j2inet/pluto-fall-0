#include "Graphics.h"
#include "Util.h"

namespace pf
{
    bool Graphics::Initialize(HWND hwnd)
    {
        m_hwnd = hwnd;
        RECT rc{};
        GetClientRect(hwnd, &rc);
        m_width = static_cast<UINT>(std::max<LONG>(rc.right - rc.left, 0));
        m_height = static_cast<UINT>(std::max<LONG>(rc.bottom - rc.top, 0));

        if (!CreateDevice())
            return false;

        if (!Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_wic)),
                   "CoCreateInstance(WICImagingFactory)"))
            return false;

        if (!Check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                       reinterpret_cast<IUnknown**>(m_dwrite.ReleaseAndGetAddressOf())),
                   "DWriteCreateFactory"))
            return false;

        CreateSizeDependentResources();
        return true;
    }

    bool Graphics::CreateDevice()
    {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT; // required for Direct2D interop
#if defined(_DEBUG)
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                             D3D_FEATURE_LEVEL_10_0 };
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                                       static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &m_device, nullptr,
                                       &m_context);
#if defined(_DEBUG)
        if (FAILED(hr))
        {
            // The debug layer may not be installed; retry without it.
            flags &= ~D3D11_CREATE_DEVICE_DEBUG;
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels,
                                   static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &m_device, nullptr,
                                   &m_context);
        }
#endif
        if (FAILED(hr))
        {
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels,
                                   static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &m_device, nullptr,
                                   &m_context);
        }
        if (!Check(hr, "D3D11CreateDevice"))
            return false;

        ComPtr<IDXGIDevice1> dxgiDevice;
        m_device.As(&dxgiDevice);
        ComPtr<IDXGIAdapter> adapter;
        dxgiDevice->GetAdapter(&adapter);
        ComPtr<IDXGIFactory2> factory;
        if (!Check(adapter->GetParent(IID_PPV_ARGS(&factory)), "IDXGIAdapter::GetParent"))
            return false;

        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = 0;
        desc.Height = 0;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        if (!Check(factory->CreateSwapChainForHwnd(m_device.Get(), m_hwnd, &desc, nullptr, nullptr, &m_swapChain),
                   "CreateSwapChainForHwnd"))
            return false;
        factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER); // we handle Alt+Enter ourselves

        D2D1_FACTORY_OPTIONS options{};
#if defined(_DEBUG)
        options.debugLevel = D2D1_DEBUG_LEVEL_WARNING;
#endif
        if (!Check(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options,
                                     reinterpret_cast<void**>(m_d2dFactory.ReleaseAndGetAddressOf())),
                   "D2D1CreateFactory"))
            return false;
        if (!Check(m_d2dFactory->CreateDevice(dxgiDevice.Get(), &m_d2dDevice), "ID2D1Factory1::CreateDevice"))
            return false;
        if (!Check(m_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_d2dContext),
                   "CreateDeviceContext"))
            return false;
        m_d2dContext->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        return true;
    }

    void Graphics::ReleaseSizeDependentResources()
    {
        if (m_d2dContext)
            m_d2dContext->SetTarget(nullptr);
        m_d2dTarget.Reset();
        if (m_context)
        {
            m_context->OMSetRenderTargets(0, nullptr, nullptr);
            m_context->Flush();
        }
        m_rtv.Reset();
        m_dsv.Reset();
    }

    void Graphics::CreateSizeDependentResources()
    {
        if (m_width == 0 || m_height == 0)
            return;

        ComPtr<ID3D11Texture2D> backBuffer;
        if (!Check(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)), "GetBuffer"))
            return;
        Check(m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_rtv), "CreateRenderTargetView");

        D3D11_TEXTURE2D_DESC depthDesc{};
        depthDesc.Width = m_width;
        depthDesc.Height = m_height;
        depthDesc.MipLevels = 1;
        depthDesc.ArraySize = 1;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Usage = D3D11_USAGE_DEFAULT;
        depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        ComPtr<ID3D11Texture2D> depth;
        if (Check(m_device->CreateTexture2D(&depthDesc, nullptr, &depth), "CreateTexture2D(depth)"))
            Check(m_device->CreateDepthStencilView(depth.Get(), nullptr, &m_dsv), "CreateDepthStencilView");

        ComPtr<IDXGISurface> surface;
        backBuffer.As(&surface);
        D2D1_BITMAP_PROPERTIES1 props{};
        props.pixelFormat = { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED };
        props.dpiX = 96.0f;
        props.dpiY = 96.0f;
        props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        if (Check(m_d2dContext->CreateBitmapFromDxgiSurface(surface.Get(), &props, &m_d2dTarget),
                  "CreateBitmapFromDxgiSurface"))
        {
            m_d2dContext->SetTarget(m_d2dTarget.Get());
            m_d2dContext->SetDpi(96.0f, 96.0f);
        }

        // Letterbox to 16:9.
        const float scale = std::min(m_width / CanvasWidth, m_height / CanvasHeight);
        m_viewport.Width = CanvasWidth * scale;
        m_viewport.Height = CanvasHeight * scale;
        m_viewport.TopLeftX = (m_width - m_viewport.Width) * 0.5f;
        m_viewport.TopLeftY = (m_height - m_viewport.Height) * 0.5f;
        m_viewport.MinDepth = 0.0f;
        m_viewport.MaxDepth = 1.0f;
    }

    void Graphics::Resize(UINT width, UINT height)
    {
        if (!m_swapChain)
            return;
        if (width == m_width && height == m_height)
            return;
        m_width = width;
        m_height = height;
        if (width == 0 || height == 0)
            return; // minimised
        ReleaseSizeDependentResources();
        Check(m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers");
        CreateSizeDependentResources();
    }

    void Graphics::BindDefaultTarget()
    {
        ID3D11RenderTargetView* rtv = m_rtv.Get();
        m_context->OMSetRenderTargets(1, &rtv, m_dsv.Get());
        m_context->RSSetViewports(1, &m_viewport);
    }

    void Graphics::BeginFrame(const Color& clear)
    {
        if (!m_rtv)
            return;
        const float color[4] = { clear.r, clear.g, clear.b, clear.a };
        m_context->ClearRenderTargetView(m_rtv.Get(), color);
        ClearDepth();
        BindDefaultTarget();
    }

    void Graphics::ClearDepth()
    {
        if (m_dsv)
            m_context->ClearDepthStencilView(m_dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    }

    void Graphics::Present()
    {
        if (!m_swapChain)
            return;
        HRESULT hr = m_swapChain->Present(1, 0);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
            Log("[PlutoFall] Graphics device lost (0x{:08X})", static_cast<uint32_t>(hr));
    }

    void Graphics::Begin2D()
    {
        if (m_in2D || !m_d2dTarget)
            return;
        m_in2D = true;
        m_d2dContext->BeginDraw();
        const float scale = m_viewport.Width / CanvasWidth;
        m_d2dContext->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) *
                                   D2D1::Matrix3x2F::Translation(m_viewport.TopLeftX, m_viewport.TopLeftY));
        // Clip so nothing is drawn into the letterbox bars.
        m_d2dContext->PushAxisAlignedClip(D2D1::RectF(0, 0, CanvasWidth, CanvasHeight), D2D1_ANTIALIAS_MODE_ALIASED);
    }

    void Graphics::End2D()
    {
        if (!m_in2D)
            return;
        m_in2D = false;
        m_d2dContext->PopAxisAlignedClip();
        HRESULT hr = m_d2dContext->EndDraw();
        if (FAILED(hr))
            Log("[PlutoFall] ID2D1DeviceContext::EndDraw failed (0x{:08X})", static_cast<uint32_t>(hr));
        // Direct2D may have changed D3D state; restore our target.
        BindDefaultTarget();
    }
}
