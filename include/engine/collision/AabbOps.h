#ifndef CONCORD_AABBOPS_H
#define CONCORD_AABBOPS_H

#include "engine/collision/Aabb.h"
#include "engine/collision/query/Ray.h"
#include "math/Vector3.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Concord::Collision {

/** True when every component is finite and min <= max on each axis. */
inline bool IsValidAabb(const Aabb& box) noexcept
{
    return std::isfinite(box.min.x) && std::isfinite(box.min.y) && std::isfinite(box.min.z)
        && std::isfinite(box.max.x) && std::isfinite(box.max.y) && std::isfinite(box.max.z)
        && box.min.x <= box.max.x && box.min.y <= box.max.y && box.min.z <= box.max.z;
}

/** Axis-aligned union of two boxes. */
inline Aabb UnionAabb(const Aabb& a, const Aabb& b) noexcept
{
    return Aabb{
        Vector3{std::min(a.min.x, b.min.x), std::min(a.min.y, b.min.y),
                std::min(a.min.z, b.min.z)},
        Vector3{std::max(a.max.x, b.max.x), std::max(a.max.y, b.max.y),
                std::max(a.max.z, b.max.z)},
    };
}

/** Surface area of the six faces; used by SAH-style tree insertion. */
inline float AabbSurfaceArea(const Aabb& box) noexcept
{
    const float dx = box.max.x - box.min.x;
    const float dy = box.max.y - box.min.y;
    const float dz = box.max.z - box.min.z;
    return 2.0f * (dx * dy + dy * dz + dz * dx);
}

/** Expands every side of @p box by @p margin (clamped to >= 0). */
inline Aabb FattenAabb(const Aabb& box, float margin) noexcept
{
    const float m = margin > 0.0f ? margin : 0.0f;
    return Aabb{
        Vector3{box.min.x - m, box.min.y - m, box.min.z - m},
        Vector3{box.max.x + m, box.max.y + m, box.max.z + m},
    };
}

/** True when @p inner is fully inside @p outer (touching counts as contained). */
inline bool AabbContainsAabb(const Aabb& outer, const Aabb& inner) noexcept
{
    return outer.min.x <= inner.min.x && outer.min.y <= inner.min.y
        && outer.min.z <= inner.min.z && outer.max.x >= inner.max.x
        && outer.max.y >= inner.max.y && outer.max.z >= inner.max.z;
}

/**
 * Intersects a normalized world ray with an AABB.
 * @return true and writes entry distance into @p outT when the ray hits within
 *         [minDistance, maxDistance]; rejects non-finite or inverted boxes.
 */
inline bool IntersectRayAabb(const Ray& normalizedRay, const Aabb& box,
                             float minDistance, float maxDistance,
                             float& outT) noexcept
{
    if (!IsValidAabb(box) || !std::isfinite(minDistance) || !std::isfinite(maxDistance)
        || minDistance < 0.0f || maxDistance < minDistance) {
        return false;
    }

    float tMin = minDistance;
    float tMax = maxDistance;
    const float origin[3] = {normalizedRay.origin.x, normalizedRay.origin.y,
                             normalizedRay.origin.z};
    const float direction[3] = {normalizedRay.direction.x, normalizedRay.direction.y,
                                normalizedRay.direction.z};
    const float bmin[3] = {box.min.x, box.min.y, box.min.z};
    const float bmax[3] = {box.max.x, box.max.y, box.max.z};

    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 1.0e-8f) {
            if (origin[axis] < bmin[axis] || origin[axis] > bmax[axis]) {
                return false;
            }
            continue;
        }
        const float inv = 1.0f / direction[axis];
        float t0 = (bmin[axis] - origin[axis]) * inv;
        float t1 = (bmax[axis] - origin[axis]) * inv;
        if (t0 > t1) {
            std::swap(t0, t1);
        }
        tMin = std::max(tMin, t0);
        tMax = std::min(tMax, t1);
        if (tMin > tMax) {
            return false;
        }
    }

    outT = tMin;
    return true;
}

/**
 * True when a solid AABB of half-extents @p halfExtents, swept from @p origin
 * along the normalized @p direction for up to @p maxDistance, would overlap
 * @p box. Implemented as a ray vs expanded box (AABB Minkowski sum).
 */
inline bool IntersectSweepAabb(const Vector3& origin, const Vector3& direction,
                               float halfExtents, float maxDistance,
                               const Aabb& box, float& outT) noexcept
{
    if (!IsValidAabb(box) || halfExtents < 0.0f || maxDistance < 0.0f
        || !std::isfinite(halfExtents) || !std::isfinite(maxDistance)) {
        return false;
    }
    const Aabb expanded{
        Vector3{box.min.x - halfExtents, box.min.y - halfExtents,
                box.min.z - halfExtents},
        Vector3{box.max.x + halfExtents, box.max.y + halfExtents,
                box.max.z + halfExtents},
    };
    if (expanded.Contains(origin)) {
        outT = 0.0f;
        return true;
    }
    const Ray ray{origin, direction};
    return IntersectRayAabb(ray, expanded, 0.0f, maxDistance, outT);
}

} // namespace Concord::Collision

#endif // CONCORD_AABBOPS_H
