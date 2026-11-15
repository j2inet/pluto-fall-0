// Renderer3D.h - draws Models with shaders generated from their material definitions.
//
// The pixel shader is compiled on demand as permutations of the material features
// (base colour texture, emissive texture, alpha mask, unlit), so each glTF material
// gets a shader that only does the work it needs.
#pragma once
#include "Common.h"

namespace pf
{
    class Graphics;
    struct Model;

    namespace ShaderFeature
    {
        constexpr uint32_t BaseColorTexture = 1u << 0;
        constexpr uint32_t EmissiveTexture = 1u << 1;
        constexpr uint32_t AlphaMask = 1u << 2;
        constexpr uint32_t Unlit = 1u << 3;
    }

    class Renderer3D
    {
    public:
        // The gameplay camera looks down +Z at the z = 0 plane and frames exactly
        // 32 x 18 world units there (16:9).
        static constexpr float ViewHalfHeight = 9.0f;
        static constexpr float ViewHalfWidth = 16.0f;
        static constexpr float FieldOfView = Pi / 4.0f;
        static float CameraDistance();
        static XMMATRIX GameView();
        static XMMATRIX GameProjection();
        // Converts a point on the 1920x1080 canvas to a world position on the z = 0 plane.
        static XMFLOAT3 CanvasToWorld(float x, float y);
        // Converts a world position on the z = 0 plane to canvas coordinates.
        static D2D1_POINT_2F WorldToCanvas(XMFLOAT3 world);

        bool Initialize(Graphics& graphics);

        void BeginScene(FXMMATRIX view, CXMMATRIX projection, XMFLOAT3 cameraPosition);
        void BeginGameScene(); // uses the standard gameplay camera

        // tint multiplies the material colour (alpha < 1 renders translucent); flash is added.
        void Draw(const Model& model, FXMMATRIX world, XMFLOAT4 tint = { 1, 1, 1, 1 }, XMFLOAT3 flash = { 0, 0, 0 });

    private:
        ID3D11PixelShader* PixelShader(uint32_t features);

        Graphics* m_graphics = nullptr;
        ComPtr<ID3D11VertexShader> m_vertexShader;
        ComPtr<ID3D11InputLayout> m_inputLayout;
        std::unordered_map<uint32_t, ComPtr<ID3D11PixelShader>> m_pixelShaders;
        ComPtr<ID3D11Buffer> m_frameCB;
        ComPtr<ID3D11Buffer> m_objectCB;
        ComPtr<ID3D11Buffer> m_materialCB;
        ComPtr<ID3D11RasterizerState> m_cullBack;
        ComPtr<ID3D11RasterizerState> m_cullNone;
        ComPtr<ID3D11DepthStencilState> m_depthWrite;
        ComPtr<ID3D11DepthStencilState> m_depthRead;
        ComPtr<ID3D11BlendState> m_opaque;
        ComPtr<ID3D11BlendState> m_alphaBlend;
        ComPtr<ID3D11SamplerState> m_sampler;
    };
}
