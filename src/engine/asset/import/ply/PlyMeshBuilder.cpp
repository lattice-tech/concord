#include "engine/asset/import/ply/PlyMeshBuilder.h"

#include "engine/asset/import/ply/PlyDataReader.h"
#include "engine/asset/import/ply/PlyLimits.h"
#include "engine/debug/Logger.h"
#include "math/Vector2.h"
#include "math/Vector3.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Concord::Asset::Ply {

namespace {

/** Generates flat per-triangle normals for a mesh whose file omitted them. */
void GenerateFlatNormals(const std::vector<Vector3>& positions,
                         const std::vector<std::uint32_t>& indices,
                         std::vector<Vector3>& normals)
{
    normals.assign(positions.size(), Vector3{0.0f, 1.0f, 0.0f});
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        const Vector3& a = positions[indices[i]];
        const Vector3& b = positions[indices[i + 1]];
        const Vector3& c = positions[indices[i + 2]];
        const Vector3 e1{b.x - a.x, b.y - a.y, b.z - a.z};
        const Vector3 e2{c.x - a.x, c.y - a.y, c.z - a.z};
        Vector3 n{e1.y * e2.z - e1.z * e2.y,
                  e1.z * e2.x - e1.x * e2.z,
                  e1.x * e2.y - e1.y * e2.x};
        const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len > 0.0f) {
            n.x /= len; n.y /= len; n.z /= len;
        } else {
            n = Vector3{0.0f, 1.0f, 0.0f};
        }
        normals[indices[i]] = n;
        normals[indices[i + 1]] = n;
        normals[indices[i + 2]] = n;
    }
}

} // namespace

bool BuildMesh(std::istream& file, const PlyHeader& header, const std::string& path, ImportedModel& model)
{
    const PlyElement* vertEl = nullptr;
    const PlyElement* faceEl = nullptr;
    for (const PlyElement& e : header.elements) {
        if (e.name == "vertex") {
            vertEl = &e;
        } else if (e.name == "face") {
            faceEl = &e;
        }
    }
    if (vertEl == nullptr) {
        Debug::Logger::Warn("Asset", "PLY: no 'vertex' element in '%s'", path.c_str());
        return false;
    }
    if (vertEl->count > Limits::MaxVertexCount ||
        (faceEl != nullptr && faceEl->count > Limits::MaxFaceCount)) {
        Debug::Logger::Warn("Asset", "PLY: declared geometry exceeds import limits in '%s'",
                            path.c_str());
        return false;
    }

    const VertexLayout layout = ResolveVertexLayout(*vertEl);
    if (!layout.HasPosition()) {
        Debug::Logger::Warn("Asset", "PLY: vertex element lacks x/y/z in '%s'", path.c_str());
        return false;
    }
    const bool wantNormals = layout.HasNormals();

    std::vector<Vector3> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> uvs;
    if (!ReadVertices(file, header.binary, *vertEl, layout, positions, normals, uvs)) {
        Debug::Logger::Warn("Asset", "PLY: malformed or truncated vertex data in '%s'", path.c_str());
        return false;
    }

    std::vector<std::uint32_t> indices;
    if (faceEl != nullptr &&
        !ReadFaces(file, header.binary, *faceEl,
                   static_cast<std::uint32_t>(positions.size()), indices)) {
        Debug::Logger::Warn("Asset", "PLY: malformed face data or index limit exceeded in '%s'",
                            path.c_str());
        return false;
    }

    if (positions.empty()) {
        Debug::Logger::Warn("Asset", "PLY: no vertices in '%s'", path.c_str());
        return false;
    }

    if (!wantNormals) {
        GenerateFlatNormals(positions, indices, normals);
    }

    ImportedSubMesh sub;
    sub.geometry.positions = std::move(positions);
    sub.geometry.normals = std::move(normals);
    sub.geometry.uvs = std::move(uvs);
    if (sub.geometry.positions.size() <= 65535 && !indices.empty()) {
        sub.geometry.indices.reserve(indices.size());
        for (std::uint32_t i : indices) {
            sub.geometry.indices.push_back(static_cast<std::uint16_t>(i));
        }
    } else {
        sub.geometry.indices32 = std::move(indices);
    }
    model.meshes.push_back(std::move(sub));

    if (!model.meshes.empty() && !model.meshes[0].geometry.positions.empty()) {
        const auto& pos = model.meshes[0].geometry.positions;
        model.boundsMin = model.boundsMax = pos[0];
        for (const Vector3& p : pos) {
            model.boundsMin.x = std::min(model.boundsMin.x, p.x);
            model.boundsMin.y = std::min(model.boundsMin.y, p.y);
            model.boundsMin.z = std::min(model.boundsMin.z, p.z);
            model.boundsMax.x = std::max(model.boundsMax.x, p.x);
            model.boundsMax.y = std::max(model.boundsMax.y, p.y);
            model.boundsMax.z = std::max(model.boundsMax.z, p.z);
        }
    }
    return true;
}

} // namespace Concord::Asset::Ply
