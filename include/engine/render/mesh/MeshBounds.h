#ifndef CONCORD_MESHBOUNDS_H
#define CONCORD_MESHBOUNDS_H

#include "engine/collision/Aabb.h"
#include "engine/render/mesh/MeshData.h"

namespace Concord {

/**
 * @brief Model-space AABB spanning a mesh's vertex positions.
 *
 * Computed once on the CPU when geometry is finalized, so frustum culling can
 * bound an imported mesh by its real extents instead of assuming the unit cube
 * that built-in primitives use. Non-finite positions are skipped rather than
 * poisoning the box, because a single bad vertex would otherwise cull the whole
 * mesh for the rest of the run.
 *
 * @param data Geometry to measure; only `positions` is read.
 * @param outBounds Receives the box only when this returns true.
 * @return false when @p data has no finite position at all.
 */
bool ComputeMeshBounds(const MeshData& data, Collision::Aabb& outBounds) noexcept;

} // namespace Concord

#endif // CONCORD_MESHBOUNDS_H
