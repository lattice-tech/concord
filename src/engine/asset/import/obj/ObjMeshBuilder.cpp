#include "engine/asset/import/obj/ObjMeshBuilder.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace Concord::Asset::Obj {

std::uint32_t Group::Add(const VertexKey& key,
                         const std::vector<Vector3>& srcPos,
                         const std::vector<Vector2>& srcUv,
                         const std::vector<Vector3>& srcNrm)
{
    const auto it = lookup.find(key);
    if (it != lookup.end()) {
        return it->second;
    }
    const std::uint32_t idx = static_cast<std::uint32_t>(positions.size());
    if (key.pos >= srcPos.size()) {
        positions.push_back(Vector3{});
    } else {
        positions.push_back(srcPos[key.pos]);
    }
    if (key.uv != kInvalid && key.uv < srcUv.size()) {
        uvs.push_back(srcUv[key.uv]);
        hasUvs = true;
    } else {
        uvs.push_back(Vector2{0.0f, 0.0f});
    }
    if (key.nrm != kInvalid && key.nrm < srcNrm.size()) {
        normals.push_back(srcNrm[key.nrm]);
        hasNormals = true;
    } else {
        normals.push_back(Vector3{0.0f, 1.0f, 0.0f});
    }
    lookup.emplace(key, idx);
    return idx;
}

VertexKey ParseFaceVertex(const std::string& token,
                          std::size_t posCount,
                          std::size_t uvCount,
                          std::size_t nrmCount)
{
    VertexKey key{Group::kInvalid, Group::kInvalid, Group::kInvalid};
    // Split "p", "p/t", "p//n", "p/t/n" on '/'.
    std::size_t start = 0;
    int field = 0;
    for (std::size_t i = 0; i <= token.size(); ++i) {
        if (i == token.size() || token[i] == '/') {
            if (i > start) {
                const long val = std::strtol(token.data() + start, nullptr, 10);
                std::uint32_t idx;
                if (val < 0) {
                    // Negative indices count from the end of the current stream.
                    const long resolved = (field == 0 ? static_cast<long>(posCount)
                                         : field == 1 ? static_cast<long>(uvCount)
                                         : static_cast<long>(nrmCount)) + val;
                    idx = resolved > 0 ? static_cast<std::uint32_t>(resolved - 1) : Group::kInvalid;
                } else if (val > 0) {
                    idx = static_cast<std::uint32_t>(val - 1); // OBJ is 1-based
                } else {
                    idx = Group::kInvalid;
                }
                if (field == 0) {
                    key.pos = idx;
                } else if (field == 1) {
                    key.uv = idx;
                } else {
                    key.nrm = idx;
                }
            }
            ++field;
            start = i + 1;
        }
    }
    return key;
}

void GenerateFlatNormals(Group& group)
{
    if (group.hasNormals) {
        return;
    }
    group.normals.assign(group.positions.size(), Vector3{0.0f, 1.0f, 0.0f});
    for (std::size_t i = 0; i + 2 < group.indices.size(); i += 3) {
        const std::uint32_t a = group.indices[i];
        const std::uint32_t b = group.indices[i + 1];
        const std::uint32_t c = group.indices[i + 2];
        const Vector3 e1{group.positions[b].x - group.positions[a].x,
                         group.positions[b].y - group.positions[a].y,
                         group.positions[b].z - group.positions[a].z};
        const Vector3 e2{group.positions[c].x - group.positions[a].x,
                         group.positions[c].y - group.positions[a].y,
                         group.positions[c].z - group.positions[a].z};
        Vector3 n{e1.y * e2.z - e1.z * e2.y,
                  e1.z * e2.x - e1.x * e2.z,
                  e1.x * e2.y - e1.y * e2.x};
        const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len > 0.0f) {
            n.x /= len; n.y /= len; n.z /= len;
        } else {
            n = Vector3{0.0f, 1.0f, 0.0f};
        }
        group.normals[a] = n;
        group.normals[b] = n;
        group.normals[c] = n;
    }
}

ImportedSubMesh Finalize(const Group& group, const MaterialTable& materials)
{
    ImportedSubMesh sub;
    sub.geometry.positions = group.positions;
    sub.geometry.normals = group.normals;
    sub.geometry.uvs = group.uvs;

    // Use 16-bit indices when the vertex count fits; otherwise 32-bit so large
    // models never have to be split.
    if (group.positions.size() <= 65535) {
        sub.geometry.indices.reserve(group.indices.size());
        for (std::uint32_t idx : group.indices) {
            sub.geometry.indices.push_back(static_cast<std::uint16_t>(idx));
        }
    } else {
        sub.geometry.indices32 = group.indices;
    }

    const auto it = materials.find(group.materialName);
    if (it != materials.end()) {
        sub.material = it->second.desc;
    }
    return sub;
}

} // namespace Concord::Asset::Obj
