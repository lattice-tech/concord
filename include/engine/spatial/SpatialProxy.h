#ifndef CONCORD_SPATIALPROXY_H
#define CONCORD_SPATIALPROXY_H

#include "engine/collision/Aabb.h"
#include "engine/spatial/SpatialId.h"

#include <cstdint>

namespace Concord::Spatial {

/**
 * @brief One authored object tracked by a DynamicAabbTree.
 *
 * The tree stores only the fat AABB, a user payload, and optional layer bits.
 * Callers keep the generation-safe SpatialId; they never hold raw node pointers.
 */
struct SpatialProxy {
    Collision::Aabb bounds{};
    /** Opaque user payload (ObjectHandle bits, mesh instance index, ...). */
    std::uint64_t userData = 0;
    /** Membership layer; queries may mask against this field. */
    std::uint32_t layer = 1u;
};

/**
 * @brief Mask applied by tree queries.
 *
 * A proxy participates when `(proxy.layer & mask) != 0`. The default mask keeps
 * every non-zero layer visible, matching the collider layer default of 1.
 */
struct SpatialQueryFilter {
    std::uint32_t layerMask = 0xFFFFFFFFu;
};

} // namespace Concord::Spatial

#endif // CONCORD_SPATIALPROXY_H
