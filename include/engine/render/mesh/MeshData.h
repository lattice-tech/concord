#ifndef CONCORD_MESHDATA_H
#define CONCORD_MESHDATA_H

#include "math/Vector2.h"
#include "math/Vector3.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Concord {

/**
 * CPU-side description of a mesh, handed to IRenderBackend::CreateMesh to
 * upload into GPU buffers.
 *
 * This is backend-agnostic geometry, not a live GPU resource: the backend
 * copies whatever it needs out of it during CreateMesh, so a MeshData may be
 * a temporary that is discarded straight after (see Primitives). The layout
 * is intentionally minimal for now — positions plus a 16-bit index buffer —
 * matching what the current shader consumes; richer per-vertex attributes
 * (normals, UVs) are added here as extra fields once the shading path grows
 * to use them, without disturbing existing callers.
 */
struct MeshData {
    /** One entry per vertex, in world/model units. */
    std::vector<Vector3> positions;

    /**
     * Outward unit normal per vertex, parallel to `positions` (same length).
     * Consumed by the lighting shader; a face gets flat shading by giving its
     * vertices the same normal, smooth shading by averaging across faces.
     */
    std::vector<Vector3> normals;

    /**
     * Texture coordinate per vertex, parallel to `positions` (same length).
     * Sampled by textured materials; a generator that omits UVs leaves this
     * empty and the backend defaults each vertex to (0, 0).
     */
    std::vector<Vector2> uvs;

    /**
     * Skinning: up to four bone (joint) indices influencing each vertex,
     * parallel to `positions`. Populated only for skinned meshes (glTF with a
     * skin); empty for static meshes, in which case the backend uploads the
     * non-skinned vertex layout. Indices are into the mesh's Skeleton bones.
     */
    std::vector<std::array<std::uint16_t, 4>> boneIndices;

    /**
     * Skinning: the four blend weights matching `boneIndices` per vertex,
     * parallel to `positions`. Should sum to ~1 per vertex; the importer
     * normalises them. Empty exactly when `boneIndices` is empty.
     */
    std::vector<std::array<float, 4>> boneWeights;

    /** True when this mesh carries per-vertex skinning data (bone indices/weights). */
    bool HasSkin() const noexcept { return !boneIndices.empty(); }

    /**
     * Triangle list into `positions` as 16-bit indices; length must be a
     * multiple of three. Use this when the vertex count fits in 65535 (the
     * built-in primitives always do); prefer `indices32` for larger meshes
     * so imported models never have to be split. Exactly one of `indices` /
     * `indices32` should be populated.
     */
    std::vector<std::uint16_t> indices;

    /**
     * Triangle list into `positions` as 32-bit indices; used when a mesh has
     * more than 65535 vertices (common for imported models). The backend picks
     * this up instead of `indices` when it is non-empty, creating a 32-bit
     * index buffer. Leave empty for small meshes that use `indices`.
     */
    std::vector<std::uint32_t> indices32;
};

} // namespace Concord

#endif // CONCORD_MESHDATA_H
