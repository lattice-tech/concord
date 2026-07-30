#ifndef CONCORD_FRUSTUMCULLER_H
#define CONCORD_FRUSTUMCULLER_H

#include "engine/spatial/DynamicAabbTree.h"
#include "engine/spatial/Frustum.h"
#include "engine/spatial/VisibilityStats.h"

#include <functional>
#include <vector>

namespace Concord::Spatial {

/**
 * @brief Walks a DynamicAabbTree against a world frustum, recording telemetry.
 *
 * The culler is a free function set (no state) so it can run on workers. Callers
 * supply a visitor for each surviving proxy; the returned VisibilityStats is
 * independent of visitor side effects.
 */
VisibilityStats CullFrustum(
    const DynamicAabbTree& tree, const Frustum& frustum, SpatialQueryFilter filter,
    const std::function<void(SpatialId, const SpatialProxy&)>& visitor);

/**
 * Brute-force oracle: tests every live proxy with FrustumIntersectsAabb.
 * Used by regressions to prove the tree walk matches linear scanning.
 */
VisibilityStats CullFrustumBruteForce(
    const DynamicAabbTree& tree, const Frustum& frustum, SpatialQueryFilter filter,
    const std::function<void(SpatialId, const SpatialProxy&)>& visitor);

/** Collects surviving SpatialIds into @p out (cleared first). */
void CollectVisibleIds(const DynamicAabbTree& tree, const Frustum& frustum,
                       SpatialQueryFilter filter, std::vector<SpatialId>& out);

} // namespace Concord::Spatial

#endif // CONCORD_FRUSTUMCULLER_H
