#ifndef CONCORD_RAYCASTFILTER_H
#define CONCORD_RAYCASTFILTER_H

#include "engine/object/ObjectId.h"

#include <cstdint>
#include <limits>

namespace Concord::Collision {

/**
 * @brief Bounds and target filtering applied to one gameplay ray query.
 *
 * `layerMask` is directional: a collider participates when any bit in its
 * occupied layer intersects this mask. It does not use the collider's own scan
 * mask because a ray is a query, not another collision participant.
 */
struct RaycastFilter {
    /** Occupied collider layers the query may hit. */
    std::uint32_t layerMask = std::numeric_limits<std::uint32_t>::max();

    /** Inclusive start of the tested ray interval, in world units. */
    float minDistance = 0.0f;

    /** Inclusive end of the tested ray interval, in world units. */
    float maxDistance = std::numeric_limits<float>::infinity();

    /** One collider to exclude, or kInvalidObjectId to exclude none. */
    Object::ObjectId ignoreColliderId = Object::kInvalidObjectId;
};

} // namespace Concord::Collision

#endif // CONCORD_RAYCASTFILTER_H
