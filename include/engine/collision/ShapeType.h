#ifndef CONCORD_SHAPETYPE_H
#define CONCORD_SHAPETYPE_H

namespace Concord::Collision {

/**
 * The geometric form a CollisionShape takes.
 *
 * Both forms resolve to a world-space axis-aligned bounding box for overlap
 * tests (a "collision box"): a Box uses its half-extents directly, a Sphere
 * uses its radius on every axis. New forms are added here and given their
 * extent rule in CollisionShape / Collider.
 */
enum class ShapeType {
    /** A box defined by per-axis half-extents. */
    Box,

    /** A sphere defined by a radius. */
    Sphere,
};

/** Canonical, human-readable name of a ShapeType (never null). */
inline const char* ToString(ShapeType type) noexcept
{
    switch (type) {
        case ShapeType::Box:    return "Box";
        case ShapeType::Sphere: return "Sphere";
    }
    return "Box";
}

} // namespace Concord::Collision

#endif // CONCORD_SHAPETYPE_H
