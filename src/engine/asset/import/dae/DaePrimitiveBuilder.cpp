#include "engine/asset/import/dae/DaePrimitiveBuilder.h"

#include "engine/asset/import/dae/DaePrimitiveInputs.h"
#include "engine/asset/import/dae/DaeSourceTable.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Concord::Asset::Dae {

namespace {

/**
 * De-duplication key for a Collada vertex corner: the (position, normal, uv)
 * index triple read from <p>. Two corners with the same triple share one
 * vertex; differing triples stay distinct, preserving hard edges and UV seams.
 */
struct CornerKey {
    int pos;
    int nrm;
    int uv;
    bool operator==(const CornerKey& o) const noexcept { return pos == o.pos && nrm == o.nrm && uv == o.uv; }
};

struct CornerKeyHash {
    std::size_t operator()(const CornerKey& k) const noexcept
    {
        std::uint64_t h = 1469598103934665603ULL;
        h ^= static_cast<std::uint32_t>(k.pos); h *= 1099511628211ULL;
        h ^= static_cast<std::uint32_t>(k.nrm); h *= 1099511628211ULL;
        h ^= static_cast<std::uint32_t>(k.uv);  h *= 1099511628211ULL;
        return static_cast<std::size_t>(h);
    }
};

} // namespace

DaeBuiltSubMesh BuildFromPrimitive(const XmlNode& primitive,
                                   const XmlNode& mesh,
                                   const DaeSourceTable& sources)
{
    DaeBuiltSubMesh sub;
    sub.materialSymbol = primitive.Attr("material");

    std::vector<InputInfo> inputs;
    const int setWidth = CollectInputs(primitive, inputs);
    if (setWidth <= 0) {
        return sub;
    }

    // Resolve the source ids for each semantic we care about.
    const std::string posSource = FindSourceForSemantic(inputs, mesh, "VERTEX");
    const std::string nrmSource = FindSourceForSemantic(inputs, mesh, "NORMAL");
    const std::string uvSource = FindSourceForSemantic(inputs, mesh, "TEXCOORD");
    const int posOffset = FindOffsetForSemantic(inputs, "VERTEX");
    const int nrmOffset = FindOffsetForSemantic(inputs, "NORMAL");
    const int uvOffset = FindOffsetForSemantic(inputs, "TEXCOORD");

    const DaeSource* posSrc = sources.Find(posSource);
    const DaeSource* nrmSrc = nrmSource.empty() ? nullptr : sources.Find(nrmSource);
    const DaeSource* uvSrc = uvSource.empty() ? nullptr : sources.Find(uvSource);
    if (posSrc == nullptr || posSrc->stride < 3) {
        return sub;
    }

    const std::vector<CornerSet> corners =
        CollectCorners(primitive, setWidth, posOffset, nrmOffset, uvOffset);
    if (corners.size() < 3) {
        return sub;
    }

    // De-duplicate corners and emit triangle indices. For polylist the corners
    // were laid out per-polygon; we fan-triangulate on the fly using the
    // original vcounts. To keep it simple and unified, we treat the corner
    // array as already-expanded and emit fan triangles per polygon.
    const bool isPolylist = (primitive.name == "polylist");
    std::unordered_map<CornerKey, std::uint32_t, CornerKeyHash> lookup;
    std::vector<Vector3> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> uvs;
    std::vector<std::uint32_t> outIndices;
    bool hasNormals = (nrmSrc != nullptr);
    bool hasUvs = (uvSrc != nullptr);

    auto emitCorner = [&](const CornerSet& cs) -> std::uint32_t {
        CornerKey key{cs.pos, cs.nrm, cs.uv};
        const auto it = lookup.find(key);
        if (it != lookup.end()) {
            return it->second;
        }
        const std::uint32_t idx = static_cast<std::uint32_t>(positions.size());
        Vector3 p{};
        if (cs.pos >= 0 && static_cast<std::size_t>(cs.pos * posSrc->stride + 2) < posSrc->floats.size()) {
            p.x = posSrc->floats[cs.pos * posSrc->stride + 0];
            p.y = posSrc->floats[cs.pos * posSrc->stride + 1];
            p.z = posSrc->floats[cs.pos * posSrc->stride + 2];
        }
        positions.push_back(p);
        if (hasNormals && nrmSrc != nullptr && nrmSrc->stride >= 3 &&
            cs.nrm >= 0 && static_cast<std::size_t>(cs.nrm * nrmSrc->stride + 2) < nrmSrc->floats.size()) {
            normals.push_back(Vector3{
                nrmSrc->floats[cs.nrm * nrmSrc->stride + 0],
                nrmSrc->floats[cs.nrm * nrmSrc->stride + 1],
                nrmSrc->floats[cs.nrm * nrmSrc->stride + 2]});
        } else {
            normals.push_back(Vector3{0.0f, 1.0f, 0.0f});
        }
        if (hasUvs && uvSrc != nullptr && uvSrc->stride >= 2 &&
            cs.uv >= 0 && static_cast<std::size_t>(cs.uv * uvSrc->stride + 1) < uvSrc->floats.size()) {
            uvs.push_back(Vector2{
                uvSrc->floats[cs.uv * uvSrc->stride + 0],
                uvSrc->floats[cs.uv * uvSrc->stride + 1]});
        } else {
            uvs.push_back(Vector2{0.0f, 0.0f});
        }
        lookup.emplace(key, idx);
        return idx;
    };

    if (isPolylist) {
        // Re-walk vcounts to emit fan triangles.
        const XmlNode* vcountNode = primitive.FindChild("vcount");
        if (vcountNode == nullptr) {
            return sub;
        }
        const std::vector<int> vcounts = ParseIndexList(vcountNode->text);
        std::size_t ci = 0;
        for (int vc : vcounts) {
            if (vc < 3 || ci + static_cast<std::size_t>(vc) > corners.size()) {
                ci += static_cast<std::size_t>(vc);
                continue;
            }
            const std::uint32_t i0 = emitCorner(corners[ci]);
            for (int v = 1; v + 1 < vc; ++v) {
                const std::uint32_t i1 = emitCorner(corners[ci + v]);
                const std::uint32_t i2 = emitCorner(corners[ci + v + 1]);
                outIndices.push_back(i0);
                outIndices.push_back(i1);
                outIndices.push_back(i2);
            }
            ci += static_cast<std::size_t>(vc);
        }
    } else {
        // Triangles: every 3 corners is one triangle.
        for (std::size_t i = 0; i + 2 < corners.size(); i += 3) {
            outIndices.push_back(emitCorner(corners[i]));
            outIndices.push_back(emitCorner(corners[i + 1]));
            outIndices.push_back(emitCorner(corners[i + 2]));
        }
    }

    // Generate flat normals when the file omitted them.
    if (!hasNormals) {
        normals.assign(positions.size(), Vector3{0.0f, 1.0f, 0.0f});
        for (std::size_t i = 0; i + 2 < outIndices.size(); i += 3) {
            const Vector3& a = positions[outIndices[i]];
            const Vector3& b = positions[outIndices[i + 1]];
            const Vector3& c = positions[outIndices[i + 2]];
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
            normals[outIndices[i]] = n;
            normals[outIndices[i + 1]] = n;
            normals[outIndices[i + 2]] = n;
        }
    }

    sub.geometry.positions = std::move(positions);
    sub.geometry.normals = std::move(normals);
    sub.geometry.uvs = std::move(uvs);
    if (sub.geometry.positions.size() <= 65535) {
        sub.geometry.indices.reserve(outIndices.size());
        for (std::uint32_t idx : outIndices) {
            sub.geometry.indices.push_back(static_cast<std::uint16_t>(idx));
        }
    } else {
        sub.geometry.indices32 = std::move(outIndices);
    }
    return sub;
}

} // namespace Concord::Asset::Dae
