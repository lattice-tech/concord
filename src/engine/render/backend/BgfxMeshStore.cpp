#include "engine/render/backend/BgfxMeshStore.h"

#include "engine/debug/Logger.h"
#include "engine/render/backend/IRenderBackend.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Concord {

BgfxMeshStore::BgfxMeshStore()
{
    InitLayout();
}

void BgfxMeshStore::InitLayout()
{
    // Position + normal + UV: the lit model shades from per-vertex normals and
    // textured materials sample by UV.
    m_meshLayout.begin()
        .add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();

    // Skinned layout: the same, plus 4 bone indices (Uint8, read as float in
    // vs_mesh_skinned — up to 256 bones) and 4 blend weights.
    m_skinnedLayout.begin()
        .add(bgfx::Attrib::Position,  3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal,    3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Indices,   4, bgfx::AttribType::Uint8)
        .add(bgfx::Attrib::Weight,    4, bgfx::AttribType::Float)
        .end();
}

bgfx::VertexBufferHandle BgfxMeshStore::CreateStaticVertexBuffer(const MeshData& data) const
{
    constexpr std::size_t kFloatsPerVertex = 8; // 3 pos + 3 normal + 2 uv
    const std::size_t vertexCount = data.positions.size();
    const bgfx::Memory* vbMem = bgfx::alloc(
        static_cast<std::uint32_t>(vertexCount * sizeof(float) * kFloatsPerVertex));
    auto* vertices = reinterpret_cast<float*>(vbMem->data);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        const Vector3& p = data.positions[i];
        const Vector3 n = i < data.normals.size() ? data.normals[i] : Vector3{0.0f, 1.0f, 0.0f};
        const Vector2 uv = i < data.uvs.size() ? data.uvs[i] : Vector2{0.0f, 0.0f};
        float* v = vertices + i * kFloatsPerVertex;
        v[0] = p.x; v[1] = p.y; v[2] = p.z;
        v[3] = n.x; v[4] = n.y; v[5] = n.z;
        v[6] = uv.x; v[7] = uv.y;
    }
    return bgfx::createVertexBuffer(vbMem, m_meshLayout);
}

bgfx::VertexBufferHandle BgfxMeshStore::CreateSkinnedVertexBuffer(const MeshData& data) const
{
    // Stride: 8 floats (pos/normal/uv) + 4 bytes (indices) + 4 floats (weights).
    constexpr std::size_t kStride = 8 * sizeof(float) + 4 * sizeof(std::uint8_t) + 4 * sizeof(float);
    const std::size_t vertexCount = data.positions.size();
    const bgfx::Memory* vbMem = bgfx::alloc(static_cast<std::uint32_t>(vertexCount * kStride));
    auto* base = reinterpret_cast<std::uint8_t*>(vbMem->data);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        const Vector3& p = data.positions[i];
        const Vector3 n = i < data.normals.size() ? data.normals[i] : Vector3{0.0f, 1.0f, 0.0f};
        const Vector2 uv = i < data.uvs.size() ? data.uvs[i] : Vector2{0.0f, 0.0f};
        const std::array<std::uint16_t, 4> idx =
            i < data.boneIndices.size() ? data.boneIndices[i] : std::array<std::uint16_t, 4>{0, 0, 0, 0};
        const std::array<float, 4> wt =
            i < data.boneWeights.size() ? data.boneWeights[i] : std::array<float, 4>{1.0f, 0.0f, 0.0f, 0.0f};

        std::uint8_t* v = base + i * kStride;
        auto* f = reinterpret_cast<float*>(v);
        f[0] = p.x; f[1] = p.y; f[2] = p.z;
        f[3] = n.x; f[4] = n.y; f[5] = n.z;
        f[6] = uv.x; f[7] = uv.y;
        std::uint8_t* bi = v + 8 * sizeof(float);
        bi[0] = static_cast<std::uint8_t>(idx[0]);
        bi[1] = static_cast<std::uint8_t>(idx[1]);
        bi[2] = static_cast<std::uint8_t>(idx[2]);
        bi[3] = static_cast<std::uint8_t>(idx[3]);
        auto* w = reinterpret_cast<float*>(v + 8 * sizeof(float) + 4 * sizeof(std::uint8_t));
        w[0] = wt[0]; w[1] = wt[1]; w[2] = wt[2]; w[3] = wt[3];
    }
    return bgfx::createVertexBuffer(vbMem, m_skinnedLayout);
}

MeshHandle BgfxMeshStore::Create(const MeshData& data)
{
    if (data.positions.empty() || (data.indices.empty() && data.indices32.empty())) {
        Debug::Logger::Error("Render", "CreateMesh called with no geometry");
        return MeshHandle::Invalid();
    }

    if (data.HasSkin()) {
        if (data.boneIndices.size() != data.positions.size()) {
            Debug::Logger::Error("Render", "skinned mesh bone-index count does not match vertex count");
            return MeshHandle::Invalid();
        }
        for (const std::array<std::uint16_t, 4>& indices : data.boneIndices) {
            for (std::uint16_t index : indices) {
                if (index >= kMaxRenderBones) {
                    Debug::Logger::Error("Render", "skinned mesh bone index %u exceeds the %u-bone limit",
                                         index, kMaxRenderBones);
                    return MeshHandle::Invalid();
                }
            }
        }
    }

    // Interleave into a bgfx-owned vertex buffer matching the layout. Copy
    // (not makeRef): MeshData is caller-owned and often a temporary (see
    // Primitives), so it cannot be referenced past this call. Skinned meshes
    // (bone indices/weights present) take the wider skinned layout; static
    // meshes the compact pos/normal/uv one. Normals default to +Y, UVs (0,0).
    BgfxMesh mesh;
    mesh.skinned = data.HasSkin();
    mesh.vb = mesh.skinned ? CreateSkinnedVertexBuffer(data) : CreateStaticVertexBuffer(data);

    // 32-bit indices for large imported meshes; 16-bit for the small built-in
    // primitives. bgfx::createIndexBuffer's `flags` selects the index width.
    if (!data.indices32.empty()) {
        const bgfx::Memory* ibMem = bgfx::copy(
            data.indices32.data(),
            static_cast<std::uint32_t>(data.indices32.size() * sizeof(std::uint32_t)));
        mesh.ib = bgfx::createIndexBuffer(ibMem, BGFX_BUFFER_INDEX32);
        mesh.indexCount = static_cast<std::uint32_t>(data.indices32.size());
    } else {
        const bgfx::Memory* ibMem = bgfx::copy(
            data.indices.data(),
            static_cast<std::uint32_t>(data.indices.size() * sizeof(std::uint16_t)));
        mesh.ib = bgfx::createIndexBuffer(ibMem);
        mesh.indexCount = static_cast<std::uint32_t>(data.indices.size());
    }

    if (!bgfx::isValid(mesh.vb) || !bgfx::isValid(mesh.ib)) {
        Debug::Logger::Error("Render", "mesh buffer creation failed");
        if (bgfx::isValid(mesh.vb)) {
            bgfx::destroy(mesh.vb);
        }
        if (bgfx::isValid(mesh.ib)) {
            bgfx::destroy(mesh.ib);
        }
        return MeshHandle::Invalid();
    }

    // Local-space AABB of the uploaded positions, kept CPU-side so the shadow
    // frustum can fit the scene from the pending draws without round-tripping
    // vertex data back from the GPU.
    mesh.aabbMin[0] = mesh.aabbMin[1] = mesh.aabbMin[2] = std::numeric_limits<float>::max();
    mesh.aabbMax[0] = mesh.aabbMax[1] = mesh.aabbMax[2] = std::numeric_limits<float>::lowest();
    for (const Vector3& p : data.positions) {
        mesh.aabbMin[0] = std::min(mesh.aabbMin[0], p.x);
        mesh.aabbMin[1] = std::min(mesh.aabbMin[1], p.y);
        mesh.aabbMin[2] = std::min(mesh.aabbMin[2], p.z);
        mesh.aabbMax[0] = std::max(mesh.aabbMax[0], p.x);
        mesh.aabbMax[1] = std::max(mesh.aabbMax[1], p.y);
        mesh.aabbMax[2] = std::max(mesh.aabbMax[2], p.z);
    }

    if (mesh.skinned) {
        mesh.boneAabbs.resize(kMaxRenderBones);
        for (std::array<float, 6>& bounds : mesh.boneAabbs) {
            bounds = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                      std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
        }
        for (std::size_t vertex = 0; vertex < data.positions.size(); ++vertex) {
            const Vector3& p = data.positions[vertex];
            const std::array<std::uint16_t, 4>& indices = data.boneIndices[vertex];
            const std::array<float, 4>& weights = vertex < data.boneWeights.size()
                ? data.boneWeights[vertex]
                : std::array<float, 4>{1.0f, 0.0f, 0.0f, 0.0f};
            for (std::size_t influence = 0; influence < 4; ++influence) {
                if (weights[influence] <= 0.0f) {
                    continue;
                }
                std::array<float, 6>& bounds = mesh.boneAabbs[indices[influence]];
                bounds[0] = std::min(bounds[0], p.x);
                bounds[1] = std::min(bounds[1], p.y);
                bounds[2] = std::min(bounds[2], p.z);
                bounds[3] = std::max(bounds[3], p.x);
                bounds[4] = std::max(bounds[4], p.y);
                bounds[5] = std::max(bounds[5], p.z);
            }
        }
    }

    Debug::Logger::Debug("Render", "uploaded mesh: %zu verts, %u indices (%s)",
                         data.positions.size(), mesh.indexCount,
                         data.indices32.empty() ? "16-bit" : "32-bit");
    return m_meshes.Add(mesh);
}

void BgfxMeshStore::Destroy(MeshHandle mesh)
{
    BgfxMesh removed;
    if (!m_meshes.Remove(mesh, &removed)) {
        return;
    }
    if (bgfx::isValid(removed.ib)) {
        bgfx::destroy(removed.ib);
    }
    if (bgfx::isValid(removed.vb)) {
        bgfx::destroy(removed.vb);
    }
}

void BgfxMeshStore::Clear()
{
    m_meshes.ForEach([](MeshHandle, BgfxMesh& mesh) {
        if (bgfx::isValid(mesh.ib)) {
            bgfx::destroy(mesh.ib);
        }
        if (bgfx::isValid(mesh.vb)) {
            bgfx::destroy(mesh.vb);
        }
    });
    m_meshes.Clear();
}

} // namespace Concord
