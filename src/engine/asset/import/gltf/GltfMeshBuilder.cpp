#include "engine/asset/import/gltf/GltfMeshBuilder.h"

#include "engine/asset/import/gltf/GltfBufferReader.h"
#include "engine/asset/import/gltf/GltfMaterialBuilder.h"
#include "math/Vector2.h"
#include "math/Vector3.h"

#include <array>
#include <cmath>
#include <cstddef>

namespace Concord::Asset::Gltf {

namespace {

/**
 * Generates area-weighted smooth vertex normals from the triangle list, for
 * meshes whose glTF has no NORMAL attribute (e.g. the Khronos Fox). Without
 * this the mesh gets a constant default normal that skinning then rotates into
 * garbage per bone — banding and ripples across the surface.
 */
void GenerateSmoothNormals(MeshData& mesh)
{
    mesh.normals.assign(mesh.positions.size(), Vector3{0.0f, 0.0f, 0.0f});
    const std::size_t vertexCount = mesh.positions.size();

    auto accumulate = [&](std::uint32_t ia, std::uint32_t ib, std::uint32_t ic) {
        if (ia >= vertexCount || ib >= vertexCount || ic >= vertexCount) {
            return;
        }
        const Vector3& a = mesh.positions[ia];
        const Vector3& b = mesh.positions[ib];
        const Vector3& c = mesh.positions[ic];
        const Vector3 e1{b.x - a.x, b.y - a.y, b.z - a.z};
        const Vector3 e2{c.x - a.x, c.y - a.y, c.z - a.z};
        // Cross product, left unnormalised so larger faces weigh more.
        const Vector3 n{
            e1.y * e2.z - e1.z * e2.y,
            e1.z * e2.x - e1.x * e2.z,
            e1.x * e2.y - e1.y * e2.x,
        };
        for (std::uint32_t idx : {ia, ib, ic}) {
            mesh.normals[idx].x += n.x;
            mesh.normals[idx].y += n.y;
            mesh.normals[idx].z += n.z;
        }
    };

    if (!mesh.indices32.empty()) {
        for (std::size_t i = 0; i + 2 < mesh.indices32.size(); i += 3) {
            accumulate(mesh.indices32[i], mesh.indices32[i + 1], mesh.indices32[i + 2]);
        }
    } else {
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            accumulate(mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]);
        }
    }

    for (Vector3& n : mesh.normals) {
        const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len > 1e-8f) {
            n.x /= len;
            n.y /= len;
            n.z /= len;
        } else {
            n = Vector3{0.0f, 1.0f, 0.0f};
        }
    }
}

} // namespace

ImportedSubMesh ImportPrimitive(const JsonValue& primitive,
                                const std::vector<JsonValue>& accessors,
                                const std::vector<std::vector<std::uint8_t>>& buffers,
                                const std::vector<JsonValue>& bufferViews,
                                const std::vector<JsonValue>& materials,
                                const std::vector<JsonValue>& textures,
                                const std::vector<JsonValue>& images,
                                const std::string& dir,
                                float transform[16])
{
    ImportedSubMesh sub;
    const JsonValue* attrs = primitive.Find("attributes");
    if (attrs == nullptr || !attrs->IsObject()) {
        return sub;
    }

    const int posIdx = attrs->IntOr("POSITION", -1);
    if (posIdx < 0 || posIdx >= static_cast<int>(accessors.size())) {
        return sub;
    }
    const JsonValue& posAcc = accessors[posIdx];
    const int posCount = posAcc.IntOr("count", 0);
    if (posCount <= 0) {
        return sub;
    }

    // Positions (mandatory).
    sub.geometry.positions.resize(posCount);
    for (int i = 0; i < posCount; ++i) {
        const auto v = ReadAccessor(posAcc, buffers, bufferViews, i);
        // Bake the node transform (column-major 4x4).
        const float x = v[0], y = v[1], z = v[2];
        sub.geometry.positions[i] = Vector3{
            transform[0] * x + transform[4] * y + transform[8] * z + transform[12],
            transform[1] * x + transform[5] * y + transform[9] * z + transform[13],
            transform[2] * x + transform[6] * y + transform[10] * z + transform[14]};
    }

    // Normals (optional).
    const int nrmIdx = attrs->IntOr("NORMAL", -1);
    if (nrmIdx >= 0 && nrmIdx < static_cast<int>(accessors.size())) {
        const JsonValue& nrmAcc = accessors[nrmIdx];
        sub.geometry.normals.resize(posCount);
        for (int i = 0; i < posCount; ++i) {
            const auto v = ReadAccessor(nrmAcc, buffers, bufferViews, i);
            // Rotate the normal by the upper-left 3x3 (ignore translation).
            const float x = v[0], y = v[1], z = v[2];
            sub.geometry.normals[i] = Vector3{
                transform[0] * x + transform[4] * y + transform[8] * z,
                transform[1] * x + transform[5] * y + transform[9] * z,
                transform[2] * x + transform[6] * y + transform[10] * z};
        }
    }

    // UV0 (optional).
    const int uvIdx = attrs->IntOr("TEXCOORD_0", -1);
    if (uvIdx >= 0 && uvIdx < static_cast<int>(accessors.size())) {
        const JsonValue& uvAcc = accessors[uvIdx];
        sub.geometry.uvs.resize(posCount);
        for (int i = 0; i < posCount; ++i) {
            const auto v = ReadAccessor(uvAcc, buffers, bufferViews, i);
            sub.geometry.uvs[i] = Vector2{v[0], v[1]};
        }
    }

    // Indices (optional — when absent, the primitive is non-indexed).
    const int idxIdx = primitive.IntOr("indices", -1);
    const bool use32 = posCount > 65535;
    if (idxIdx >= 0 && idxIdx < static_cast<int>(accessors.size())) {
        const JsonValue& idxAcc = accessors[idxIdx];
        const int idxCount = idxAcc.IntOr("count", 0);
        if (use32) {
            sub.geometry.indices32.resize(idxCount);
        } else {
            sub.geometry.indices.resize(idxCount);
        }
        for (int i = 0; i < idxCount; ++i) {
            const auto v = ReadAccessor(idxAcc, buffers, bufferViews, i);
            const std::uint32_t idx = static_cast<std::uint32_t>(v[0]);
            if (use32) {
                sub.geometry.indices32[i] = idx;
            } else {
                sub.geometry.indices[i] = static_cast<std::uint16_t>(idx);
            }
        }
    } else {
        // Non-indexed: synthesize 0..N-1.
        if (use32) {
            sub.geometry.indices32.resize(posCount);
            for (int i = 0; i < posCount; ++i) {
                sub.geometry.indices32[i] = i;
            }
        } else {
            sub.geometry.indices.resize(posCount);
            for (int i = 0; i < posCount; ++i) {
                sub.geometry.indices[i] = static_cast<std::uint16_t>(i);
            }
        }
    }

    // Material.
    const int matIdx = primitive.IntOr("material", -1);
    if (matIdx >= 0 && matIdx < static_cast<int>(materials.size())) {
        sub.material = BuildMaterial(materials[matIdx], textures, images, buffers, bufferViews, dir);
    }

    // Some glTF meshes ship no NORMAL attribute (e.g. Fox). Synthesize smooth
    // normals from the geometry so lighting — and skinning of those normals —
    // is well defined instead of a constant default.
    if (sub.geometry.normals.empty() && !sub.geometry.positions.empty()) {
        GenerateSmoothNormals(sub.geometry);
    }

    return sub;
}

} // namespace Concord::Asset::Gltf
