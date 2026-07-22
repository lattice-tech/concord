#ifndef CONCORD_TRANSFORM_H
#define CONCORD_TRANSFORM_H

#include "math/Quaternion.h"
#include "math/Vector3.h"

namespace Concord {

/**
 * Position, rotation and scale in world space (or whatever space the
 * caller is using — Concord does not impose a scene graph yet).
 *
 * A plain aggregate so a caller can build one with designated
 * initializers, e.g. `Transform{.position = {.y = 2.0f}}`. Rotation is
 * stored as a Quaternion so repeatedly composing rotations (see
 * Object::Box::Rotate) stays well-behaved; use Quaternion::FromEuler
 * when you want to specify angles in degrees without manual trig.
 */
struct Transform {
    Vector3 position{};
    Quaternion rotation{};
    Vector3 scale{1.0f, 1.0f, 1.0f};
};

} // namespace Concord

#endif // CONCORD_TRANSFORM_H
