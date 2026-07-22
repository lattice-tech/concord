#ifndef CONCORD_COLLIDERDESC_H
#define CONCORD_COLLIDERDESC_H

#include "engine/collision/CollisionShape.h"
#include "engine/object/Transform.h"

#include <cstdint>

namespace Concord::Object {

/**
 * Every field a Collider can be constructed from.
 *
 * A plain aggregate, like the other *Desc types, so a caller names only what
 * it wants, e.g. `ColliderDesc{.shape = {.type = ShapeType::Sphere, .radius = 2.0f}}`.
 *
 * A Collider is usually parented to the object it guards (via SetParent), so it
 * rides that object's world transform; `transform` here is the collider's own
 * local placement on top of the parent, left at identity in the common case.
 */
struct ColliderDesc {
    /** The customizable shape this collider tests with. */
    Collision::CollisionShape shape{};

    /** Local placement relative to the parent (identity by default). */
    Transform transform{};

    /**
     * Collision layer / mask bitfields, the same filtering Godot uses. `layer`
     * is the set of layers this collider *occupies*; `mask` is the set of layers
     * it *scans* for. Two colliders only notify each other when their sets
     * cross, i.e. one scans a layer the other occupies. Both default to layer 1
     * (bit 0), so out of the box every collider collides with every other -
     * matching the previous behaviour before filtering existed.
     */
    std::uint32_t layer = 1;
    std::uint32_t mask = 1;
};

} // namespace Concord::Object

#endif // CONCORD_COLLIDERDESC_H
