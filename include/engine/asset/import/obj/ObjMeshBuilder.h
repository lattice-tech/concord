#ifndef CONCORD_OBJ_MESHBUILDER_H
#define CONCORD_OBJ_MESHBUILDER_H

#include "engine/asset/import/ImportedModel.h"
#include "engine/asset/import/obj/ObjMaterialLibrary.h"
#include "math/Vector2.h"
#include "math/Vector3.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Concord::Asset::Obj {

/**
 * De-duplication key for an OBJ vertex corner: the (position, uv, normal)
 * index triple from the face line. Two corners with the same triple are the
 * same vertex; differing triples (even sharing a position) are distinct, which
 * is what preserves hard edges and UV seams.
 */
struct VertexKey {
    std::uint32_t pos;
    std::uint32_t uv;
    std::uint32_t nrm;
    bool operator==(const VertexKey& o) const noexcept
    {
        return pos == o.pos && uv == o.uv && nrm == o.nrm;
    }
};

/** Hash for VertexKey: an FNV-1a-style mix of its three 32-bit indices. */
struct VertexKeyHash {
    std::size_t operator()(const VertexKey& k) const noexcept
    {
        std::uint64_t h = 1469598103934665603ULL;
        h ^= k.pos;  h *= 1099511628211ULL;
        h ^= k.uv;   h *= 1099511628211ULL;
        h ^= k.nrm;  h *= 1099511628211ULL;
        return static_cast<std::size_t>(h);
    }
};

/**
 * One material group being assembled: its own vertex/index buffer and a
 * de-dup map. OBJ separates faces by `usemtl`, so each group becomes one
 * ImportedSubMesh with its own material.
 */
struct Group {
    std::vector<Vector3> positions;
    std::vector<Vector3> normals;
    std::vector<Vector2> uvs;
    std::vector<std::uint32_t> indices;
    std::unordered_map<VertexKey, std::uint32_t, VertexKeyHash> lookup;
    std::string materialName;
    bool hasNormals = false;
    bool hasUvs = false;

    static constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;

    /** Inserts (or reuses) a corner and returns its index in this group. */
    std::uint32_t Add(const VertexKey& key,
                      const std::vector<Vector3>& srcPos,
                      const std::vector<Vector2>& srcUv,
                      const std::vector<Vector3>& srcNrm);
};

/**
 * Parses one face-line vertex token ("p", "p/t", "p//n", "p/t/n") into a
 * de-dup key, resolving 1-based and negative OBJ indices against the current
 * stream sizes. Absent fields become Group::kInvalid.
 */
VertexKey ParseFaceVertex(const std::string& token,
                          std::size_t posCount,
                          std::size_t uvCount,
                          std::size_t nrmCount);

/** Generates flat per-face normals for a group that had none in the file. */
void GenerateFlatNormals(Group& group);

/** Finalizes a group into a sub-mesh, choosing 16- or 32-bit indices. */
ImportedSubMesh Finalize(const Group& group, const MaterialTable& materials);

} // namespace Concord::Asset::Obj

#endif // CONCORD_OBJ_MESHBUILDER_H
