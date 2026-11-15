// Gltf.cpp - a compact glTF 2.0 loader (.gltf with external/embedded buffers, and binary .glb).
//
// Supported: node hierarchy (matrix or TRS), triangle primitives, POSITION / NORMAL / TEXCOORD_0 /
// COLOR_0 attributes (float or normalized integer), all index types, metallic-roughness materials
// (base colour + emissive factors/textures, alpha modes, double sided) and KHR_materials_unlit.
// Animation and skinning are not used by the game, so the scene is baked in its bind pose.
#include "Json.h"
#include "Model.h"
#include "Graphics.h"
#include "Texture.h"
#include "Util.h"

namespace pf
{
    namespace
    {
        constexpr uint32_t GlbMagic = 0x46546C67;     // "glTF"
        constexpr uint32_t ChunkJson = 0x4E4F534A;    // "JSON"
        constexpr uint32_t ChunkBin = 0x004E4942;     // "BIN\0"

        std::optional<std::vector<uint8_t>> DecodeBase64(std::string_view text)
        {
            auto value = [](char c) -> int
            {
                if (c >= 'A' && c <= 'Z') return c - 'A';
                if (c >= 'a' && c <= 'z') return c - 'a' + 26;
                if (c >= '0' && c <= '9') return c - '0' + 52;
                if (c == '+' || c == '-') return 62;
                if (c == '/' || c == '_') return 63;
                return -1;
            };
            std::vector<uint8_t> out;
            out.reserve(text.size() * 3 / 4);
            uint32_t buffer = 0;
            int bits = 0;
            for (char c : text)
            {
                if (c == '=' || c == '\r' || c == '\n' || c == ' ')
                    continue;
                int v = value(c);
                if (v < 0)
                    return std::nullopt;
                buffer = (buffer << 6) | static_cast<uint32_t>(v);
                bits += 6;
                if (bits >= 8)
                {
                    bits -= 8;
                    out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
                }
            }
            return out;
        }

        std::string PercentDecode(std::string_view uri)
        {
            std::string out;
            for (size_t i = 0; i < uri.size(); ++i)
            {
                if (uri[i] == '%' && i + 2 < uri.size())
                {
                    auto hex = [](char c) -> int
                    {
                        if (c >= '0' && c <= '9') return c - '0';
                        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                        return -1;
                    };
                    int hi = hex(uri[i + 1]), lo = hex(uri[i + 2]);
                    if (hi >= 0 && lo >= 0)
                    {
                        out += static_cast<char>(hi * 16 + lo);
                        i += 2;
                        continue;
                    }
                }
                out += uri[i];
            }
            return out;
        }

        // Returns the payload of a data: URI, or nullopt if the URI is not a data URI.
        std::optional<std::vector<uint8_t>> DecodeDataUri(std::string_view uri)
        {
            if (!uri.starts_with("data:"))
                return std::nullopt;
            auto comma = uri.find(',');
            if (comma == std::string_view::npos)
                return std::vector<uint8_t>{};
            std::string_view header = uri.substr(0, comma);
            std::string_view payload = uri.substr(comma + 1);
            if (header.ends_with(";base64"))
                return DecodeBase64(payload).value_or(std::vector<uint8_t>{});
            std::string decoded = PercentDecode(payload);
            return std::vector<uint8_t>(decoded.begin(), decoded.end());
        }

        int ComponentCount(std::string_view type)
        {
            if (type == "SCALAR") return 1;
            if (type == "VEC2") return 2;
            if (type == "VEC3") return 3;
            if (type == "VEC4") return 4;
            if (type == "MAT2") return 4;
            if (type == "MAT3") return 9;
            if (type == "MAT4") return 16;
            return 0;
        }

        size_t ComponentSize(int componentType)
        {
            switch (componentType)
            {
            case 5120: case 5121: return 1;
            case 5122: case 5123: return 2;
            case 5125: case 5126: return 4;
            default: return 0;
            }
        }

        class GltfDocument
        {
        public:
            GltfDocument(Graphics& graphics, TextureCache& textures, fs::path file, fs::path textureFolder)
                : m_graphics(graphics), m_textures(textures), m_file(std::move(file)), m_textureFolder(std::move(textureFolder))
            {
            }

            std::shared_ptr<Model> Load()
            {
                auto bytes = ReadAllBytes(m_file);
                if (!bytes || bytes->size() < 4)
                    return nullptr;

                std::optional<std::vector<uint8_t>> glbBin;
                std::string jsonText;
                uint32_t magic = 0;
                std::memcpy(&magic, bytes->data(), 4);
                if (magic == GlbMagic)
                {
                    if (!ParseGlb(*bytes, jsonText, glbBin))
                        return nullptr;
                }
                else
                {
                    jsonText.assign(bytes->begin(), bytes->end());
                }

                auto parsed = Json::Parse(jsonText);
                if (!parsed)
                {
                    Log("[PlutoFall] glTF {}: {}", Narrow(m_file.wstring()), parsed.error());
                    return nullptr;
                }
                m_doc = std::move(*parsed);

                if (!LoadBuffers(glbBin))
                    return nullptr;
                LoadMaterials();
                CollectMeshes();
                if (m_meshes.empty())
                    return nullptr;
                return BuildModel(m_graphics.Device(), m_meshes, std::move(m_materials));
            }

        private:
            struct BufferViewData
            {
                const uint8_t* data = nullptr;
                size_t length = 0;
                size_t stride = 0;
            };

            struct Accessor
            {
                const uint8_t* data = nullptr;
                size_t count = 0;
                size_t stride = 0;
                int componentType = 0;
                int components = 0;
                bool normalized = false;
                std::vector<uint8_t> zeros; // storage for accessors without a buffer view

                float Float(size_t index, int component) const
                {
                    const uint8_t* p = data + index * stride + static_cast<size_t>(component) * ComponentSize(componentType);
                    switch (componentType)
                    {
                    case 5126: { float v; std::memcpy(&v, p, 4); return v; }
                    case 5121: { uint8_t v = *p; return normalized ? v / 255.0f : static_cast<float>(v); }
                    case 5120: { int8_t v; std::memcpy(&v, p, 1); return normalized ? std::max(v / 127.0f, -1.0f) : static_cast<float>(v); }
                    case 5123: { uint16_t v; std::memcpy(&v, p, 2); return normalized ? v / 65535.0f : static_cast<float>(v); }
                    case 5122: { int16_t v; std::memcpy(&v, p, 2); return normalized ? std::max(v / 32767.0f, -1.0f) : static_cast<float>(v); }
                    case 5125: { uint32_t v; std::memcpy(&v, p, 4); return static_cast<float>(v); }
                    default: return 0.0f;
                    }
                }

                uint32_t Index(size_t index) const
                {
                    const uint8_t* p = data + index * stride;
                    switch (componentType)
                    {
                    case 5121: return *p;
                    case 5123: { uint16_t v; std::memcpy(&v, p, 2); return v; }
                    case 5125: { uint32_t v; std::memcpy(&v, p, 4); return v; }
                    default: return 0;
                    }
                }
            };

            bool ParseGlb(const std::vector<uint8_t>& bytes, std::string& jsonText, std::optional<std::vector<uint8_t>>& bin)
            {
                if (bytes.size() < 20)
                    return false;
                uint32_t length = 0;
                std::memcpy(&length, bytes.data() + 8, 4);
                length = std::min<uint32_t>(length, static_cast<uint32_t>(bytes.size()));
                size_t offset = 12;
                while (offset + 8 <= length)
                {
                    uint32_t chunkLength = 0, chunkType = 0;
                    std::memcpy(&chunkLength, bytes.data() + offset, 4);
                    std::memcpy(&chunkType, bytes.data() + offset + 4, 4);
                    offset += 8;
                    if (offset + chunkLength > length)
                        return false;
                    const uint8_t* chunk = bytes.data() + offset;
                    if (chunkType == ChunkJson)
                        jsonText.assign(reinterpret_cast<const char*>(chunk), chunkLength);
                    else if (chunkType == ChunkBin && !bin)
                        bin = std::vector<uint8_t>(chunk, chunk + chunkLength);
                    offset += (chunkLength + 3u) & ~3u;
                }
                return !jsonText.empty();
            }

            bool LoadBuffers(std::optional<std::vector<uint8_t>>& glbBin)
            {
                const Json& buffers = m_doc["buffers"];
                for (size_t i = 0; i < buffers.Size(); ++i)
                {
                    const Json& buffer = buffers[i];
                    const std::string& uri = buffer["uri"].AsString();
                    std::vector<uint8_t> data;
                    if (uri.empty())
                    {
                        if (i == 0 && glbBin)
                            data = std::move(*glbBin);
                    }
                    else if (auto inline_ = DecodeDataUri(uri))
                    {
                        data = std::move(*inline_);
                    }
                    else
                    {
                        auto file = ReadAllBytes(m_file.parent_path() / fs::path(Widen(PercentDecode(uri))));
                        if (!file)
                        {
                            Log("[PlutoFall] glTF buffer '{}' not found", uri);
                            return false;
                        }
                        data = std::move(*file);
                    }
                    const size_t declared = static_cast<size_t>(buffer.GetFloat("byteLength", 0));
                    if (data.size() < declared)
                    {
                        Log("[PlutoFall] glTF buffer {} is truncated", i);
                        return false;
                    }
                    m_buffers.push_back(std::move(data));
                }
                return true;
            }

            std::optional<BufferViewData> BufferView(int index) const
            {
                const Json& view = m_doc["bufferViews"][static_cast<size_t>(index)];
                if (!view.IsObject())
                    return std::nullopt;
                const size_t buffer = static_cast<size_t>(view.GetInt("buffer", 0));
                if (buffer >= m_buffers.size())
                    return std::nullopt;
                const size_t offset = static_cast<size_t>(view.GetFloat("byteOffset", 0));
                const size_t length = static_cast<size_t>(view.GetFloat("byteLength", 0));
                if (offset + length > m_buffers[buffer].size())
                    return std::nullopt;
                BufferViewData data;
                data.data = m_buffers[buffer].data() + offset;
                data.length = length;
                data.stride = static_cast<size_t>(view.GetInt("byteStride", 0));
                return data;
            }

            std::optional<Accessor> GetAccessor(int index) const
            {
                const Json& acc = m_doc["accessors"][static_cast<size_t>(index)];
                if (!acc.IsObject())
                    return std::nullopt;
                Accessor a;
                a.componentType = acc.GetInt("componentType", 5126);
                a.components = ComponentCount(acc["type"].AsString());
                a.count = static_cast<size_t>(acc.GetFloat("count", 0));
                a.normalized = acc.GetBool("normalized", false);
                const size_t elementSize = ComponentSize(a.componentType) * static_cast<size_t>(a.components);
                if (elementSize == 0)
                    return std::nullopt;
                if (!acc.Has("bufferView"))
                {
                    a.zeros.assign(elementSize * std::max<size_t>(a.count, 1), 0);
                    a.data = a.zeros.data();
                    a.stride = elementSize;
                    return a;
                }
                auto view = BufferView(acc.GetInt("bufferView", 0));
                if (!view)
                    return std::nullopt;
                const size_t offset = static_cast<size_t>(acc.GetFloat("byteOffset", 0));
                a.stride = view->stride ? view->stride : elementSize;
                if (a.count > 0 && offset + (a.count - 1) * a.stride + elementSize > view->length)
                    return std::nullopt;
                a.data = view->data + offset;
                return a;
            }

            // ---------------------------------------------------------------- textures & materials

            ComPtr<ID3D11ShaderResourceView> TextureFor(const Json& textureInfo, bool srgb, const Color& fallback)
            {
                if (!textureInfo.IsObject())
                    return nullptr;
                const Json& texture = m_doc["textures"][static_cast<size_t>(textureInfo.GetInt("index", -1))];
                if (!texture.IsObject() || !texture.Has("source"))
                    return m_textures.SolidColor(fallback);
                const int imageIndex = texture.GetInt("source", -1);
                const Json& image = m_doc["images"][static_cast<size_t>(imageIndex)];
                const std::string cacheKey = std::format("{}#image{}", Narrow(m_file.wstring()), imageIndex);

                ComPtr<ID3D11ShaderResourceView> srv;
                const std::string& uri = image["uri"].AsString();
                if (!uri.empty())
                {
                    if (auto bytes = DecodeDataUri(uri))
                    {
                        srv = m_textures.TryLoadMemory(*bytes, srgb, cacheKey);
                    }
                    else
                    {
                        const fs::path relative(Widen(PercentDecode(uri)));
                        const fs::path candidates[] = {
                            m_textureFolder / relative.filename(),
                            m_textureFolder / relative,
                            m_file.parent_path() / relative,
                        };
                        for (const auto& candidate : candidates)
                        {
                            if ((srv = m_textures.TryLoadFile(candidate, srgb)))
                                break;
                        }
                    }
                }
                else if (image.Has("bufferView"))
                {
                    if (auto view = BufferView(image.GetInt("bufferView", 0)))
                        srv = m_textures.TryLoadMemory(std::span<const uint8_t>(view->data, view->length), srgb, cacheKey);
                }

                if (!srv)
                {
                    Log("[PlutoFall] glTF image {} of {} could not be loaded; using a solid colour", imageIndex,
                        Narrow(m_file.filename().wstring()));
                    srv = m_textures.SolidColor(fallback);
                }
                return srv;
            }

            void LoadMaterials()
            {
                const Json& materials = m_doc["materials"];
                for (size_t i = 0; i < materials.Size(); ++i)
                {
                    const Json& m = materials[i];
                    Material mat;
                    const Json& pbr = m["pbrMetallicRoughness"];
                    const Json& baseColor = pbr["baseColorFactor"];
                    if (baseColor.Size() == 4)
                        mat.baseColor = { baseColor[0].AsFloat(1), baseColor[1].AsFloat(1), baseColor[2].AsFloat(1), baseColor[3].AsFloat(1) };
                    mat.metallic = pbr.GetFloat("metallicFactor", 1.0f);
                    mat.roughness = pbr.GetFloat("roughnessFactor", 1.0f);
                    const Json& emissive = m["emissiveFactor"];
                    if (emissive.Size() == 3)
                        mat.emissive = { emissive[0].AsFloat(), emissive[1].AsFloat(), emissive[2].AsFloat() };
                    if (const Json& strength = m["extensions"]["KHR_materials_emissive_strength"]; strength.IsObject())
                    {
                        const float s = strength.GetFloat("emissiveStrength", 1.0f);
                        mat.emissive = { mat.emissive.x * s, mat.emissive.y * s, mat.emissive.z * s };
                    }
                    const std::string alphaMode = m.GetString("alphaMode", "OPAQUE");
                    mat.alphaMode = alphaMode == "BLEND" ? AlphaMode::Blend : alphaMode == "MASK" ? AlphaMode::Mask : AlphaMode::Opaque;
                    mat.alphaCutoff = m.GetFloat("alphaCutoff", 0.5f);
                    mat.doubleSided = m.GetBool("doubleSided", false);
                    mat.unlit = m["extensions"].Has("KHR_materials_unlit");

                    mat.baseColorTexture = TextureFor(pbr["baseColorTexture"], true, Colors::White);
                    if (m["emissiveTexture"].IsObject())
                    {
                        mat.emissiveTexture = TextureFor(m["emissiveTexture"], true, Colors::White);
                        if (mat.emissive.x == 0 && mat.emissive.y == 0 && mat.emissive.z == 0)
                            mat.emissive = { 1, 1, 1 };
                    }
                    m_materials.push_back(std::move(mat));
                }
                m_defaultMaterial = static_cast<int>(m_materials.size());
                m_materials.emplace_back(); // default material for primitives without one
            }

            // ---------------------------------------------------------------- geometry

            static XMMATRIX LocalTransform(const Json& node)
            {
                const Json& matrix = node["matrix"];
                if (matrix.Size() == 16)
                {
                    // glTF matrices are column-major for column vectors; read straight into rows
                    // to get the equivalent row-vector matrix DirectXMath uses.
                    XMFLOAT4X4 m;
                    for (int r = 0; r < 4; ++r)
                        for (int c = 0; c < 4; ++c)
                            m.m[r][c] = matrix[static_cast<size_t>(r * 4 + c)].AsFloat();
                    return XMLoadFloat4x4(&m);
                }
                XMVECTOR scale = XMVectorSet(1, 1, 1, 0);
                XMVECTOR rotation = XMQuaternionIdentity();
                XMVECTOR translation = XMVectorZero();
                if (const Json& s = node["scale"]; s.Size() == 3)
                    scale = XMVectorSet(s[0].AsFloat(1), s[1].AsFloat(1), s[2].AsFloat(1), 0);
                if (const Json& r = node["rotation"]; r.Size() == 4)
                    rotation = XMQuaternionNormalize(XMVectorSet(r[0].AsFloat(), r[1].AsFloat(), r[2].AsFloat(), r[3].AsFloat(1)));
                if (const Json& t = node["translation"]; t.Size() == 3)
                    translation = XMVectorSet(t[0].AsFloat(), t[1].AsFloat(), t[2].AsFloat(), 1);
                return XMMatrixScaling(XMVectorGetX(scale), XMVectorGetY(scale), XMVectorGetZ(scale)) *
                       XMMatrixRotationQuaternion(rotation) * XMMatrixTranslationFromVector(translation);
            }

            void CollectMeshes()
            {
                const Json& nodes = m_doc["nodes"];
                std::vector<int> roots;
                const Json& scenes = m_doc["scenes"];
                if (scenes.Size() > 0)
                {
                    const Json& scene = scenes[static_cast<size_t>(std::max(0, m_doc.GetInt("scene", 0)))];
                    for (const Json& n : scene["nodes"].Items())
                        roots.push_back(n.AsInt());
                }
                else if (nodes.Size() > 0)
                {
                    std::vector<bool> isChild(nodes.Size(), false);
                    for (const Json& node : nodes.Items())
                        for (const Json& c : node["children"].Items())
                            if (c.AsInt() >= 0 && static_cast<size_t>(c.AsInt()) < isChild.size())
                                isChild[static_cast<size_t>(c.AsInt())] = true;
                    for (size_t i = 0; i < nodes.Size(); ++i)
                        if (!isChild[i])
                            roots.push_back(static_cast<int>(i));
                }

                if (roots.empty())
                {
                    // No node hierarchy: draw every mesh untransformed.
                    for (size_t i = 0; i < m_doc["meshes"].Size(); ++i)
                        AddMesh(static_cast<int>(i), XMMatrixIdentity());
                    return;
                }
                for (int root : roots)
                    VisitNode(root, XMMatrixIdentity(), 0);
            }

            void VisitNode(int index, FXMMATRIX parent, int depth)
            {
                const Json& node = m_doc["nodes"][static_cast<size_t>(index)];
                if (!node.IsObject() || depth > 64)
                    return;
                const XMMATRIX world = LocalTransform(node) * parent;
                if (node.Has("mesh"))
                    AddMesh(node.GetInt("mesh", 0), world);
                for (const Json& child : node["children"].Items())
                    VisitNode(child.AsInt(), world, depth + 1);
            }

            void AddMesh(int meshIndex, FXMMATRIX world)
            {
                const Json& mesh = m_doc["meshes"][static_cast<size_t>(meshIndex)];
                const XMMATRIX normalMatrix = XMMatrixTranspose(XMMatrixInverse(nullptr, world));
                for (const Json& prim : mesh["primitives"].Items())
                {
                    if (prim.GetInt("mode", 4) != 4)
                        continue; // triangles only
                    const Json& attributes = prim["attributes"];
                    if (!attributes.Has("POSITION"))
                        continue;
                    auto positions = GetAccessor(attributes.GetInt("POSITION", 0));
                    if (!positions || positions->components < 3 || positions->count == 0)
                        continue;
                    std::optional<Accessor> normals, uvs, colors;
                    if (attributes.Has("NORMAL"))
                        normals = GetAccessor(attributes.GetInt("NORMAL", 0));
                    if (attributes.Has("TEXCOORD_0"))
                        uvs = GetAccessor(attributes.GetInt("TEXCOORD_0", 0));
                    if (attributes.Has("COLOR_0"))
                        colors = GetAccessor(attributes.GetInt("COLOR_0", 0));

                    MeshData data;
                    const size_t count = positions->count;
                    data.vertices.resize(count);
                    for (size_t i = 0; i < count; ++i)
                    {
                        Vertex& v = data.vertices[i];
                        v.position = { positions->Float(i, 0), positions->Float(i, 1), positions->Float(i, 2) };
                        v.normal = { 0, 0, 0 };
                        if (normals && normals->count == count && normals->components >= 3)
                            v.normal = { normals->Float(i, 0), normals->Float(i, 1), normals->Float(i, 2) };
                        v.uv = { 0, 0 };
                        if (uvs && uvs->count == count && uvs->components >= 2)
                            v.uv = { uvs->Float(i, 0), uvs->Float(i, 1) };
                        v.color = { 1, 1, 1, 1 };
                        if (colors && colors->count == count && colors->components >= 3)
                            v.color = { colors->Float(i, 0), colors->Float(i, 1), colors->Float(i, 2),
                                        colors->components == 4 ? colors->Float(i, 3) : 1.0f };
                    }

                    if (prim.Has("indices"))
                    {
                        auto indices = GetAccessor(prim.GetInt("indices", 0));
                        if (!indices)
                            continue;
                        data.indices.reserve(indices->count);
                        for (size_t i = 0; i + 2 < indices->count; i += 3)
                        {
                            const uint32_t a = indices->Index(i), b = indices->Index(i + 1), c = indices->Index(i + 2);
                            if (a < count && b < count && c < count)
                                data.indices.insert(data.indices.end(), { a, b, c });
                        }
                    }
                    else
                    {
                        for (uint32_t i = 0; i + 2 < count; i += 3)
                            data.indices.insert(data.indices.end(), { i, i + 1, i + 2 });
                    }

                    const bool hasNormals = normals && normals->count == count;
                    if (!hasNormals)
                        ComputeNormals(data);

                    // Bake the node transform and convert from right-handed glTF space to our
                    // left-handed space by mirroring Z.
                    for (Vertex& v : data.vertices)
                    {
                        XMVECTOR p = XMVector3TransformCoord(XMLoadFloat3(&v.position), world);
                        XMVECTOR n = XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(&v.normal), normalMatrix));
                        XMStoreFloat3(&v.position, p);
                        XMStoreFloat3(&v.normal, n);
                        v.position.z = -v.position.z;
                        v.normal.z = -v.normal.z;
                    }
                    // A node transform with negative determinant flips winding; undo that.
                    if (XMVectorGetX(XMMatrixDeterminant(world)) < 0.0f)
                    {
                        for (size_t i = 0; i + 2 < data.indices.size(); i += 3)
                            std::swap(data.indices[i + 1], data.indices[i + 2]);
                    }

                    const int material = prim.GetInt("material", -1);
                    data.material = (material >= 0 && material < m_defaultMaterial) ? material : m_defaultMaterial;
                    m_meshes.push_back(std::move(data));
                }
            }

            static void ComputeNormals(MeshData& data)
            {
                std::vector<XMFLOAT3> accum(data.vertices.size(), XMFLOAT3{ 0, 0, 0 });
                auto add = [&](uint32_t index, FXMVECTOR n)
                {
                    XMStoreFloat3(&accum[index], XMLoadFloat3(&accum[index]) + n);
                };
                for (size_t i = 0; i + 2 < data.indices.size(); i += 3)
                {
                    const uint32_t ia = data.indices[i], ib = data.indices[i + 1], ic = data.indices[i + 2];
                    XMVECTOR a = XMLoadFloat3(&data.vertices[ia].position);
                    XMVECTOR b = XMLoadFloat3(&data.vertices[ib].position);
                    XMVECTOR c = XMLoadFloat3(&data.vertices[ic].position);
                    XMVECTOR n = XMVector3Cross(b - a, c - a); // glTF: counter-clockwise front faces
                    add(ia, n);
                    add(ib, n);
                    add(ic, n);
                }
                for (size_t i = 0; i < data.vertices.size(); ++i)
                    XMStoreFloat3(&data.vertices[i].normal, XMVector3Normalize(XMLoadFloat3(&accum[i])));
            }

            Graphics& m_graphics;
            TextureCache& m_textures;
            fs::path m_file;
            fs::path m_textureFolder;
            Json m_doc;
            std::vector<std::vector<uint8_t>> m_buffers;
            std::vector<Material> m_materials;
            std::vector<MeshData> m_meshes;
            int m_defaultMaterial = 0;
        };
    }

    std::shared_ptr<Model> LoadGltfModel(Graphics& graphics, TextureCache& textures, const fs::path& file,
                                         const fs::path& textureFolder)
    {
        GltfDocument doc(graphics, textures, file, textureFolder);
        return doc.Load();
    }
}
