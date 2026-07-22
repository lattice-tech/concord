#ifndef CONCORD_INPUTBINDING_H
#define CONCORD_INPUTBINDING_H

#include "engine/input/Key.h"
#include "engine/input/MouseButton.h"
#include "engine/input/action/ActionId.h"
#include "engine/input/action/AxisId.h"

namespace Concord {

/**
 * @brief Maps one physical Concord key or mouse button onto a digital action.
 *
 * Exactly one of `key` / `button` should be set (`Key::Unknown` and
 * `MouseButton::Count` mean "unused"). No SDL types.
 */
struct ActionBinding {
    ActionId action;
    Key key = Key::Unknown;
    MouseButton button = MouseButton::Count;
};

/**
 * @brief Maps physical inputs onto a signed axis contribution.
 *
 * Positive and negative sides are optional. Mouse delta axes use
 * `useMouseDeltaX` / `useMouseDeltaY` (mutually exclusive with keys for that
 * side). Final value is clamped to [-1, 1] after deadzone, then multiplied by
 * `sensitivity`.
 */
struct AxisBinding {
    AxisId axis;
    Key positiveKey = Key::Unknown;
    Key negativeKey = Key::Unknown;
    MouseButton positiveButton = MouseButton::Count;
    MouseButton negativeButton = MouseButton::Count;
    bool useMouseDeltaX = false;
    bool useMouseDeltaY = false;
    float sensitivity = 1.0f;
    float deadzone = 0.0f;
};

} // namespace Concord

#endif // CONCORD_INPUTBINDING_H
