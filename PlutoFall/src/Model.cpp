#include "Model.h"
#include "Graphics.h"
#include "Renderer3D.h"
#include "Texture.h"
#include "Util.h"

namespace pf
{
    uint32_t Material::ShaderFeatures() const
    {
        uint32_t features = 0;
        if (baseColorTexture)
            features |= ShaderFeature::BaseColorTexture;
        if (emissiveTexture)
            features |= ShaderFeature::EmissiveTexture;
        if (alphaMode == AlphaMode::Mask)
            features |= ShaderFeature::AlphaMask;
        if (unlit)
            features |= ShaderFeature::Unlit;
        return features;
    }

    float Model::Radius() const
    {
        XMVECTOR mn = XMLoadFloat3(&boundsMin);
        XMVECTOR mx = XMLoadFloat3(&boundsMax);
        return XMVectorGetX(XMVector3Length(mx - mn)) * 0.5f;
    }

    std::shared_ptr<Model> BuildModel(ID3D11Device* device, std::span<const MeshData> meshes, std::vector<Material> materials)
    {
        auto model = std::make_shared<Model>();
        if (materials.empty())
            materials.emplace_back();
        model->materials = std::move(materials);

        XMFLOAT3 mn{ FLT_MAX, FLT_MAX, FLT_MAX };
        XMFLOAT3 mx{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
        for (const MeshData& mesh : meshes)
        {
            if (mesh.vertices.empty() || mesh.indices.empty())
                continue;
            for (const Vertex& v : mesh.vertices)
            {
                mn = { std::min(mn.x, v.position.x), std::min(mn.y, v.position.y), std::min(mn.z, v.position.z) };
                mx = { std::max(mx.x, v.position.x), std::max(mx.y, v.position.y), std::max(mx.z, v.position.z) };
            }

            Primitive prim;
            D3D11_BUFFER_DESC vbDesc{};
            vbDesc.ByteWidth = static_cast<UINT>(mesh.vertices.size() * sizeof(Vertex));
            vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
            vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA vbData{ mesh.vertices.data(), 0, 0 };
            if (FAILED(device->CreateBuffer(&vbDesc, &vbData, &prim.vertexBuffer)))
                continue;

            D3D11_BUFFER_DESC ibDesc{};
            ibDesc.ByteWidth = static_cast<UINT>(mesh.indices.size() * sizeof(uint32_t));
            ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
            ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA ibData{ mesh.indices.data(), 0, 0 };
            if (FAILED(device->CreateBuffer(&ibDesc, &ibData, &prim.indexBuffer)))
                continue;

            prim.indexCount = static_cast<UINT>(mesh.indices.size());
            prim.material = std::clamp(mesh.material, 0, static_cast<int>(model->materials.size()) - 1);
            model->primitives.push_back(std::move(prim));
        }
        if (model->primitives.empty())
            return nullptr;
        model->boundsMin = mn;
        model->boundsMax = mx;
        return model;
    }

    MeshData MakeBoxMesh(XMFLOAT3 size, XMFLOAT4 color)
    {
        MeshData mesh;
        const XMFLOAT3 h{ size.x * 0.5f, size.y * 0.5f, size.z * 0.5f };
        struct Face
        {
            XMFLOAT3 n, u, v;
        };
        const Face faces[6] = {
            { { 1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },  { { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },
            { { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },  { { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
            { { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },  { { 0, 0, -1 }, { 1, 0, 0 }, { 0, 1, 0 } },
        };
        for (const Face& f : faces)
        {
            XMVECTOR n = XMLoadFloat3(&f.n);
            XMVECTOR u = XMLoadFloat3(&f.u);
            XMVECTOR v = XMLoadFloat3(&f.v);
            // Front faces are counter-clockwise on screen in our left-handed space, which requires
            // cross(u, v) to point opposite the outward normal.
            if (XMVectorGetX(XMVector3Dot(XMVector3Cross(u, v), n)) > 0.0f)
                std::swap(u, v);
            const XMVECTOR extent = XMLoadFloat3(&h);
            const XMVECTOR center = n * extent;
            const XMVECTOR du = u * extent;
            const XMVECTOR dv = v * extent;
            const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
            const XMVECTOR corners[4] = { center - du - dv, center + du - dv, center + du + dv, center - du + dv };
            const XMFLOAT2 uvs[4] = { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } };
            for (int i = 0; i < 4; ++i)
            {
                Vertex vert{};
                XMStoreFloat3(&vert.position, corners[i]);
                vert.normal = f.n;
                vert.uv = uvs[i];
                vert.color = color;
                mesh.vertices.push_back(vert);
            }
            mesh.indices.insert(mesh.indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        }
        return mesh;
    }

    // ---------------------------------------------------------------- ModelCache

    void ModelCache::Initialize(Graphics& graphics, TextureCache& textures)
    {
        m_graphics = &graphics;
        m_textures = &textures;
    }

    void ModelCache::Clear()
    {
        m_cache.clear();
    }

    std::shared_ptr<Model> ModelCache::Load(std::string_view modelPath, std::string_view textureFolder)
    {
        const std::string key = std::format("{}|{}", modelPath, textureFolder);
        if (auto it = m_cache.find(key); it != m_cache.end())
            return it->second;

        std::shared_ptr<Model> model;
        if (!modelPath.empty())
        {
            const fs::path file = Paths::Asset(modelPath);
            const fs::path textures = textureFolder.empty() ? file.parent_path() : Paths::Asset(textureFolder);
            std::error_code ec;
            if (fs::exists(file, ec))
                model = LoadGltfModel(*m_graphics, *m_textures, file, textures);
            if (!model)
                Log("[PlutoFall] Could not load model '{}'; using a random coloured cube", modelPath);
        }
        if (!model)
            model = FallbackCube();
        m_cache[key] = model;
        return model;
    }

    std::shared_ptr<Model> ModelCache::FallbackCube()
    {
        Material material;
        const Color c = RandomColor();
        material.baseColor = { c.r, c.g, c.b, 1.0f };
        material.roughness = 0.5f;
        MeshData mesh = MakeBoxMesh({ 1, 1, 1 });
        auto model = BuildModel(m_graphics->Device(), std::span<const MeshData>(&mesh, 1), { material });
        if (model)
            model->isFallback = true;
        return model;
    }

    std::shared_ptr<Model> ModelCache::Box(XMFLOAT3 size, const Color& color, bool unlit, XMFLOAT3 emissive)
    {
        const std::string key = std::format("box:{},{},{}|{},{},{},{}|{}|{},{},{}", size.x, size.y, size.z, color.r,
                                            color.g, color.b, color.a, unlit, emissive.x, emissive.y, emissive.z);
        if (auto it = m_cache.find(key); it != m_cache.end())
            return it->second;
        Material material;
        material.baseColor = color.ToFloat4();
        material.unlit = unlit;
        material.emissive = emissive;
        if (color.a < 1.0f)
            material.alphaMode = AlphaMode::Blend;
        MeshData mesh = MakeBoxMesh(size);
        auto model = BuildModel(m_graphics->Device(), std::span<const MeshData>(&mesh, 1), { material });
        m_cache[key] = model;
        return model;
    }
}
