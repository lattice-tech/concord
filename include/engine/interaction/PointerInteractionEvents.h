#ifndef CONCORD_POINTERINTERACTIONEVENTS_H
#define CONCORD_POINTERINTERACTIONEVENTS_H

#include "engine/object/ObjectId.h"
#include "math/Vector3.h"

namespace Concord::Interaction {

/**
 * @brief Published when the hovered target identity changes.
 *
 * Invalid IDs represent leaving the previous target. Geometry is world-space
 * and zeroed for that leave notification.
 */
struct PointerHoverChangedEvent {
    Object::ObjectId objectId = Object::kInvalidObjectId;
    Object::ObjectId colliderId = Object::kInvalidObjectId;
    Vector3 position{};
    Vector3 normal{};
    float distance = 0.0f;
};

/** @brief Published once when a captured press releases over the same target. */
struct PointerActivatedEvent {
    Object::ObjectId objectId = Object::kInvalidObjectId;
    Object::ObjectId colliderId = Object::kInvalidObjectId;
    Vector3 position{};
    Vector3 normal{};
    float distance = 0.0f;
};

} // namespace Concord::Interaction

#endif // CONCORD_POINTERINTERACTIONEVENTS_H
