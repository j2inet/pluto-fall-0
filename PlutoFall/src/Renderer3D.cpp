#include "Renderer3D.h"
#include "Graphics.h"
#include "Model.h"
#include "ShaderSource.h"
#include "Util.h"
#include <d3dcompiler.h>

namespace pf
{
    namespace
    {
        struct FrameConstants
        {
            XMFLOAT4X4 viewProjection;
            XMFLOAT4 lightDirection;
            XMFLOAT4 lightColor;
            XMFLOAT4 ambientColor;
            XMFLOAT4 cameraPosition;
        };

        struct ObjectConstants
        {
            XMFLOAT4X4 world;
            XMFLOAT4X4 worldInverse;
            XMFLOAT4 tint;
            XMFLOAT4 flash;
        };

        struct MaterialConstants
        {
            XMFLOAT4 baseColor;
            XMFLOAT4 emissive;
            float metallic;
            float roughness;
            float alphaCutoff;
            float pad;
        };

        ComPtr<ID3DBlob> Compile(const char* entry, const char* target, const D3D_SHADER_MACRO* defines)
        {
            UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
            flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
            flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
            ComPtr<ID3DBlob> code;
            ComPtr<ID3DBlob> errors;
            HRESULT hr = D3DCompile(ModelShaderSource, sizeof(ModelShaderSource) - 1, "ModelShader", defines, nullptr,
                                    entry, target, flags, 0, &code, &errors);
            if (FAILED(hr))
            {
                if (errors)
                    Log("[PlutoFall] Shader compile error: {}", static_cast<const char*>(errors->GetBufferPointer()));
                return nullptr;
            }
            return code;
        }

        ComPtr<ID3D11Buffer> MakeConstantBuffer(ID3D11Device* device, UINT size)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = (size + 15u) & ~15u;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            ComPtr<ID3D11Buffer> buffer;
            device->CreateBuffer(&desc, nullptr, &buffer);
            return buffer;
        }
    }

    float Renderer3D::CameraDistance()
    {
        return ViewHalfHeight / std::tan(FieldOfView * 0.5f);
    }

    XMMATRIX Renderer3D::GameView()
    {
        const XMVECTOR eye = XMVectorSet(0, 0, -CameraDistance(), 1);
        return XMMatrixLookAtLH(eye, XMVectorSet(0, 0, 0, 1), XMVectorSet(0, 1, 0, 0));
    }

    XMMATRIX Renderer3D::GameProjection()
    {
        return XMMatrixPerspectiveFovLH(FieldOfView, 16.0f / 9.0f, 1.0f, 400.0f);
    }

    XMFLOAT3 Renderer3D::CanvasToWorld(float x, float y)
    {
        return { (x / 1920.0f - 0.5f) * ViewHalfWidth * 2.0f, (0.5f - y / 1080.0f) * ViewHalfHeight * 2.0f, 0.0f };
    }

    D2D1_POINT_2F Renderer3D::WorldToCanvas(XMFLOAT3 world)
    {
        return { (world.x / (ViewHalfWidth * 2.0f) + 0.5f) * 1920.0f, (0.5f - world.y / (ViewHalfHeight * 2.0f)) * 1080.0f };
    }

    bool Renderer3D::Initialize(Graphics& graphics)
    {
        m_graphics = &graphics;
        ID3D11Device* device = graphics.Device();

        auto vsCode = Compile("VSMain", "vs_4_0", nullptr);
        if (!vsCode)
            return false;
        if (!Check(device->CreateVertexShader(vsCode->GetBufferPointer(), vsCode->GetBufferSize(), nullptr, &m_vertexShader),
                   "CreateVertexShader"))
            return false;

        const D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, normal), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, uv), D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        if (!Check(device->CreateInputLayout(layout, static_cast<UINT>(std::size(layout)), vsCode->GetBufferPointer(),
                                             vsCode->GetBufferSize(), &m_inputLayout),
                   "CreateInputLayout"))
            return false;

        m_frameCB = MakeConstantBuffer(device, sizeof(FrameConstants));
        m_objectCB = MakeConstantBuffer(device, sizeof(ObjectConstants));
        m_materialCB = MakeConstantBuffer(device, sizeof(MaterialConstants));

        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_BACK;
        raster.FrontCounterClockwise = TRUE; // glTF winding after the Z mirror (see Model.h)
        raster.DepthClipEnable = TRUE;
        device->CreateRasterizerState(&raster, &m_cullBack);
        raster.CullMode = D3D11_CULL_NONE;
        device->CreateRasterizerState(&raster, &m_cullNone);

        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = TRUE;
        depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        device->CreateDepthStencilState(&depth, &m_depthWrite);
        depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        device->CreateDepthStencilState(&depth, &m_depthRead);

        D3D11_BLEND_DESC blend{};
        blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        device->CreateBlendState(&blend, &m_opaque);
        blend.RenderTarget[0].BlendEnable = TRUE;
        blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        device->CreateBlendState(&blend, &m_alphaBlend);

        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_ANISOTROPIC;
        sampler.MaxAnisotropy = 8;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        sampler.ComparisonFunc = D3D11_COMPARISON_NEVER;
        device->CreateSamplerState(&sampler, &m_sampler);

        // Pre-compile the most common permutations to avoid hitches mid-game.
        PixelShader(0);
        PixelShader(ShaderFeature::Unlit);
        PixelShader(ShaderFeature::BaseColorTexture);
        return true;
    }

    ID3D11PixelShader* Renderer3D::PixelShader(uint32_t features)
    {
        if (auto it = m_pixelShaders.find(features); it != m_pixelShaders.end())
            return it->second.Get();

        const D3D_SHADER_MACRO defines[] = {
            { "HAS_BASE_COLOR_TEXTURE", (features & ShaderFeature::BaseColorTexture) ? "1" : "0" },
            { "HAS_EMISSIVE_TEXTURE", (features & ShaderFeature::EmissiveTexture) ? "1" : "0" },
            { "ALPHA_MASK", (features & ShaderFeature::AlphaMask) ? "1" : "0" },
            { "UNLIT", (features & ShaderFeature::Unlit) ? "1" : "0" },
            { nullptr, nullptr },
        };
        ComPtr<ID3D11PixelShader> shader;
        if (auto code = Compile("PSMain", "ps_4_0", defines))
            m_graphics->Device()->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &shader);
        if (!shader && features != 0)
        {
            // Fall back to the basic permutation rather than drawing nothing.
            ID3D11PixelShader* basic = PixelShader(0);
            m_pixelShaders[features] = basic;
            return basic;
        }
        m_pixelShaders[features] = shader;
        return shader.Get();
    }

    void Renderer3D::BeginScene(FXMMATRIX view, CXMMATRIX projection, XMFLOAT3 cameraPosition)
    {
        ID3D11DeviceContext* ctx = m_graphics->Context();
        m_graphics->BindDefaultTarget();

        FrameConstants frame{};
        XMStoreFloat4x4(&frame.viewProjection, XMMatrixTranspose(view * projection));
        XMVECTOR light = XMVector3Normalize(XMVectorSet(0.35f, -0.6f, 0.75f, 0));
        XMStoreFloat4(&frame.lightDirection, light);
        frame.lightColor = { 1.05f, 1.0f, 0.95f, 1 };
        frame.ambientColor = { 0.28f, 0.3f, 0.38f, 1 };
        frame.cameraPosition = { cameraPosition.x, cameraPosition.y, cameraPosition.z, 1 };
        ctx->UpdateSubresource(m_frameCB.Get(), 0, nullptr, &frame, 0, 0);

        ctx->IASetInputLayout(m_inputLayout.Get());
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(m_vertexShader.Get(), nullptr, 0);
        ID3D11Buffer* buffers[3] = { m_frameCB.Get(), m_objectCB.Get(), m_materialCB.Get() };
        ctx->VSSetConstantBuffers(0, 3, buffers);
        ctx->PSSetConstantBuffers(0, 3, buffers);
        ID3D11SamplerState* sampler = m_sampler.Get();
        ctx->PSSetSamplers(0, 1, &sampler);
    }

    void Renderer3D::BeginGameScene()
    {
        BeginScene(GameView(), GameProjection(), { 0, 0, -CameraDistance() });
    }

    void Renderer3D::Draw(const Model& model, FXMMATRIX world, XMFLOAT4 tint, XMFLOAT3 flash)
    {
        ID3D11DeviceContext* ctx = m_graphics->Context();

        ObjectConstants object{};
        XMStoreFloat4x4(&object.world, XMMatrixTranspose(world));
        XMStoreFloat4x4(&object.worldInverse, XMMatrixTranspose(XMMatrixInverse(nullptr, world)));
        object.tint = tint;
        object.flash = { flash.x, flash.y, flash.z, 0 };
        ctx->UpdateSubresource(m_objectCB.Get(), 0, nullptr, &object, 0, 0);

        for (const Primitive& prim : model.primitives)
        {
            const Material& material = model.materials[static_cast<size_t>(prim.material)];

            MaterialConstants mc{};
            mc.baseColor = material.baseColor;
            mc.emissive = { material.emissive.x, material.emissive.y, material.emissive.z, 0 };
            mc.metallic = material.metallic;
            mc.roughness = material.roughness;
            mc.alphaCutoff = material.alphaCutoff;
            ctx->UpdateSubresource(m_materialCB.Get(), 0, nullptr, &mc, 0, 0);

            ctx->PSSetShader(PixelShader(material.ShaderFeatures()), nullptr, 0);
            ID3D11ShaderResourceView* srvs[2] = { material.baseColorTexture.Get(), material.emissiveTexture.Get() };
            ctx->PSSetShaderResources(0, 2, srvs);

            const bool blended = material.alphaMode == AlphaMode::Blend || tint.w < 0.999f;
            ctx->OMSetBlendState(blended ? m_alphaBlend.Get() : m_opaque.Get(), nullptr, 0xFFFFFFFF);
            ctx->OMSetDepthStencilState(blended ? m_depthRead.Get() : m_depthWrite.Get(), 0);
            ctx->RSSetState(material.doubleSided ? m_cullNone.Get() : m_cullBack.Get());

            const UINT stride = sizeof(Vertex);
            const UINT offset = 0;
            ID3D11Buffer* vb = prim.vertexBuffer.Get();
            ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            ctx->IASetIndexBuffer(prim.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
            ctx->DrawIndexed(prim.indexCount, 0, 0);
        }
    }
}
