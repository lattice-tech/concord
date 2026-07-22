#ifndef CONCORD_RAYCASTHIT_H
#define CONCORD_RAYCASTHIT_H

#include "engine/collision/ShapeType.h"
#include "engine/object/ObjectId.h"
#include "math/Vector3.h"

namespace Concord::Collision {

/**
 * @brief Stable, pointer-free result of a gameplay ray query.
 *
 * IDs are scene-local node identities. `objectId` names the collider's direct
 * parent when one exists and otherwise equals `colliderId`, which lets object
 * selection identify the guarded node without retaining scene-owned pointers.
 */
struct RaycastHit {
    /** Collider node that produced the hit. */
    Object::ObjectId colliderId = Object::kInvalidObjectId;

    /** Directly guarded node, or the collider itself when it has no parent. */
    Object::ObjectId objectId = Object::kInvalidObjectId;

    /** World-space first query point within the requested distance interval. */
    Vector3 position{};

    /**
     * World-space outward geometric normal at a surface contact. When
     * `startedInside` is true there is no entry surface, so this instead points
     * opposite the ray direction.
     */
    Vector3 normal{};

    /** Distance from Ray::origin in world units. */
    float distance = 0.0f;

    /** True when the point at RaycastFilter::minDistance begins inside the shape. */
    bool startedInside = false;

    /** Collider shape that produced the result. */
    ShapeType shape = ShapeType::Box;
};

} // namespace Concord::Collision

#endif // CONCORD_RAYCASTHIT_H
