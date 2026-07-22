#ifndef CONCORD_RAYINTERSECTION_H
#define CONCORD_RAYINTERSECTION_H

#include "engine/collision/CollisionShape.h"
#include "engine/collision/query/Ray.h"
#include "engine/collision/query/RaycastHit.h"

namespace Concord::Collision {

/**
 * @brief Validates and normalizes a world ray once before shape traversal.
 * @return false for a non-finite origin/direction or a zero-length direction.
 */
bool NormalizeRay(const Ray& ray, Ray& outNormalized) noexcept;

/** Returns true when the inclusive query distance interval is well formed. */
bool IsValidRaycastRange(float minDistance, float maxDistance) noexcept;

/**
 * @brief Intersects a normalized world ray with a transformed local shape.
 *
 * The complete affine inverse is applied to the ray, so rotated boxes and
 * spheres under non-uniform scale are tested as their exact transformed shapes.
 * Singular or non-finite transforms and malformed shapes are rejected.
 * Identity fields in @p outHit are intentionally left unchanged for Scene to
 * populate from the owning collider.
 */
bool IntersectRayShape(const Ray& normalizedRay,
                       const CollisionShape& shape,
                       const float worldMatrix[16],
                       float minDistance,
                       float maxDistance,
                       RaycastHit& outHit) noexcept;

} // namespace Concord::Collision

#endif // CONCORD_RAYINTERSECTION_H
