// Model.h - GPU meshes and materials, plus a cache that loads glTF 2.0 (.gltf/.glb) files.
//
// Coordinate conventions: glTF is right-handed (+Y up, +Z toward the viewer). The game uses
// Direct3D's left-handed system, so the loader mirrors Z when baking vertices. Author models
// with the nose of the craft pointing along +X; the game rotates them to face the direction
// of travel for each orientation.
#pragma once
#include "Common.h"

namespace pf
{
    class Graphics;
    class TextureCache;

    struct Vertex
    {
        XMFLOAT3 position;
        XMFLOAT3 normal;
        XMFLOAT2 uv;
        XMFLOAT4 color;
    };

    enum class AlphaMode
    {
        Opaque,
        Mask,
        Blend
    };

    // Material parameters, following glTF's metallic-roughness model.
    struct Material
    {
        XMFLOAT4 baseColor{ 1, 1, 1, 1 };
        XMFLOAT3 emissive{ 0, 0, 0 };
        float metallic = 0.0f;
        float roughness = 0.6f;
        float alphaCutoff = 0.5f;
        AlphaMode alphaMode = AlphaMode::Opaque;
        bool doubleSided = false;
        bool unlit = false; // KHR_materials_unlit
        ComPtr<ID3D11ShaderResourceView> baseColorTexture;
        ComPtr<ID3D11ShaderResourceView> emissiveTexture;

        // Bit flags used to select a pixel shader permutation (see Renderer3D).
        uint32_t ShaderFeatures() const;
    };

    struct MeshData
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        int material = 0;
    };

    struct Primitive
    {
        ComPtr<ID3D11Buffer> vertexBuffer;
        ComPtr<ID3D11Buffer> indexBuffer;
        UINT indexCount = 0;
        int material = 0;
    };

    struct Model
    {
        std::vector<Primitive> primitives;
        std::vector<Material> materials;
        XMFLOAT3 boundsMin{ 0, 0, 0 };
        XMFLOAT3 boundsMax{ 0, 0, 0 };
        bool isFallback = false;

        float Radius() const;
    };

    // Uploads mesh data to the GPU.
    std::shared_ptr<Model> BuildModel(ID3D11Device* device, std::span<const MeshData> meshes, std::vector<Material> materials);

    // Builds an axis-aligned box mesh centred on the origin.
    MeshData MakeBoxMesh(XMFLOAT3 size, XMFLOAT4 color = { 1, 1, 1, 1 });

    class ModelCache
    {
    public:
        void Initialize(Graphics& graphics, TextureCache& textures);
        void Clear();

        // Loads a glTF/GLB model relative to the asset folder. Texture image URIs are resolved in
        // textureFolder first, then next to the model. If the model cannot be loaded a 1x1x1 cube of
        // a random colour is returned instead.
        std::shared_ptr<Model> Load(std::string_view modelPath, std::string_view textureFolder);

        // Procedural box (used for bullets, particles, etc).
        std::shared_ptr<Model> Box(XMFLOAT3 size, const Color& color, bool unlit, XMFLOAT3 emissive = { 0, 0, 0 });
        std::shared_ptr<Model> FallbackCube();

    private:
        Graphics* m_graphics = nullptr;
        TextureCache* m_textures = nullptr;
        std::unordered_map<std::string, std::shared_ptr<Model>> m_cache;
    };

    // Implemented in Gltf.cpp. Returns nullptr on failure.
    std::shared_ptr<Model> LoadGltfModel(Graphics& graphics, TextureCache& textures, const fs::path& file,
                                         const fs::path& textureFolder);
}
