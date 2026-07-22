#ifndef CONCORD_AABB_H
#define CONCORD_AABB_H

#include "math/Vector3.h"

namespace Concord::Collision {

/**
 * An axis-aligned bounding box in world space: the resolved "collision box"
 * a Collider occupies this frame.
 *
 * Colliders reduce their shape (under the node's world transform) to one of
 * these, and overlap is a cheap min/max comparison. Kept a small, dependency-
 * free value type so it can be reused freely (broadphase, culling, queries).
 */
struct Aabb {
    /** Lower corner (minimum x/y/z). */
    Vector3 min{0.0f, 0.0f, 0.0f};

    /** Upper corner (maximum x/y/z). */
    Vector3 max{0.0f, 0.0f, 0.0f};

    /** True when this box and `other` share any volume (touching counts as overlap). */
    bool Overlaps(const Aabb& other) const noexcept
    {
        return min.x <= other.max.x && max.x >= other.min.x
            && min.y <= other.max.y && max.y >= other.min.y
            && min.z <= other.max.z && max.z >= other.min.z;
    }

    /** True when world point `p` lies inside (or on the surface of) this box. */
    bool Contains(const Vector3& p) const noexcept
    {
        return p.x >= min.x && p.x <= max.x
            && p.y >= min.y && p.y <= max.y
            && p.z >= min.z && p.z <= max.z;
    }
};

} // namespace Concord::Collision

#endif // CONCORD_AABB_H
