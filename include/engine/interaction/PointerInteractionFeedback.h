#ifndef CONCORD_POINTERINTERACTIONFEEDBACK_H
#define CONCORD_POINTERINTERACTIONFEEDBACK_H

#include "engine/collision/query/RaycastHit.h"
#include "engine/object/ObjectId.h"

namespace Concord::Interaction {

/** @brief Pointer-free result and state snapshot produced by PointerInteractor. */
struct PointerInteractionFeedback {
    /** Current hover hit; meaningful only while `hovered` is true. */
    Collision::RaycastHit hit{};

    /** Stable identity captured by the current press, when one exists. */
    Object::ObjectId pressedObjectId = Object::kInvalidObjectId;
    Object::ObjectId pressedColliderId = Object::kInvalidObjectId;

    /** True when the current pointer ray hits an accepted collider. */
    bool hovered = false;

    /** True while a hit target remains captured from a press edge. */
    bool pressed = false;

    /** One-frame pulse when press and release resolve to the same target. */
    bool activated = false;

    /** True when UI ownership suppressed the scene query this frame. */
    bool blocked = false;
};

} // namespace Concord::Interaction

#endif // CONCORD_POINTERINTERACTIONFEEDBACK_H
