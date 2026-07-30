#ifndef CONCORD_SKINNEDMESHBOUNDS_H
#define CONCORD_SKINNEDMESHBOUNDS_H

#include "engine/collision/Aabb.h"
#include "engine/render/mesh/MeshData.h"
#include "math/Matrix4.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Concord {

/** Rest-pose AABB over the vertices one bone influences (weight > 0). */
struct SkinnedBoneBounds {
    std::uint16_t bone = 0;
    Collision::Aabb rest{};
};

/**
 * @brief Groups a skinned mesh's rest positions by influencing bone.
 *
 * Measured once on import, then reused every frame to derive posed bounds
 * without touching vertex data again.
 *
 * @param data Skinned geometry; `boneIndices`/`boneWeights` must be populated.
 * @param out Cleared, then filled in ascending bone order.
 * @param outHasUnweighted Set when some vertex has no positive weight. Such a
 *        vertex collapses to the model origin in the skinning shader, so posed
 *        bounds must include the origin to stay conservative.
 * @return false when @p data carries no skin or no finite position.
 */
bool ComputeSkinnedBoneBounds(const MeshData& data,
                              std::vector<SkinnedBoneBounds>& out,
                              bool& outHasUnweighted);

/**
 * @brief Conservative model-space bounds of a skinned mesh under @p palette.
 *
 * A skinned position is `sum(w_b * M_b * v)` with weights summing to one, i.e. a
 * convex combination of the points `M_b * v`. Each of those lies inside
 * `M_b * restBox_b`, so the union of those transformed boxes contains every
 * posed vertex — tight enough to cull with, and never wrongly rejecting a mesh.
 *
 * @param palette Column-major model-to-posed-model matrices, one per bone.
 * @param includeOrigin Union in the model origin (see outHasUnweighted above).
 * @return false when nothing could be bounded; @p out is then left unchanged.
 */
bool ComputeSkinnedPoseBounds(const std::vector<SkinnedBoneBounds>& boneBounds,
                              const Matrix4* palette, std::size_t paletteCount,
                              bool includeOrigin, Collision::Aabb& out);

} // namespace Concord

#endif // CONCORD_SKINNEDMESHBOUNDS_H
