#ifndef CONCORD_COLLISIONSHAPE_H
#define CONCORD_COLLISIONSHAPE_H

#include "engine/collision/ShapeType.h"
#include "math/Vector3.h"

namespace Concord::Collision {

/**
 * The customizable shape a Collider tests with — Concord's equivalent of a
 * Godot CollisionShape resource.
 *
 * A plain aggregate so a caller names only what it needs, e.g.
 * `{.type = ShapeType::Sphere, .radius = 2.0f}` or
 * `{.halfExtents = {1.0f, 0.5f, 1.0f}}`. Only the fields relevant to `type`
 * are read (`halfExtents` for Box, `radius` for Sphere); `offset` shifts the
 * shape relative to the owning node in local space, so one node can carry a
 * shape that isn't centered on it. The shape is transformed by the node's world
 * transform each frame, so moving or scaling the node moves and scales the box.
 */
struct CollisionShape {
    /** Which geometric form this shape takes. */
    ShapeType type = ShapeType::Box;

    /** Half-size on each local axis (Box only). Full size is twice this. */
    Vector3 halfExtents{0.5f, 0.5f, 0.5f};

    /** Radius (Sphere only). */
    float radius = 0.5f;

    /** Local-space offset of the shape's center from the owning node. */
    Vector3 offset{0.0f, 0.0f, 0.0f};
};

} // namespace Concord::Collision

#endif // CONCORD_COLLISIONSHAPE_H
