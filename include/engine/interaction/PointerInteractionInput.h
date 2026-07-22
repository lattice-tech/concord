#ifndef CONCORD_POINTERINTERACTIONINPUT_H
#define CONCORD_POINTERINTERACTIONINPUT_H

#include "engine/collision/query/RaycastFilter.h"

namespace Concord::Interaction {

/**
 * @brief One frame of pointer state used to query the runtime scene.
 *
 * Pixel coordinates use a top-left origin and framebuffer dimensions. Button
 * fields are transition edges supplied by the input owner, not held states.
 */
struct PointerInteractionInput {
    /** Pointer position in framebuffer pixels. */
    float pixelX = 0.0f;
    float pixelY = 0.0f;

    /** Framebuffer dimensions associated with the camera view. */
    float viewportWidth = 0.0f;
    float viewportHeight = 0.0f;

    /** True while the pointer belongs to this viewport. */
    bool pointerValid = false;

    /** True when UI routing has consumed the pointer for this frame. */
    bool blockedByUi = false;

    /** Primary-button transition edges for this frame. */
    bool pressed = false;
    bool released = false;

    /** Cancels any hover and press capture without querying the scene. */
    bool cancelled = false;

    /** Collision layers and distance interval accepted by the interaction. */
    Collision::RaycastFilter filter{};
};

} // namespace Concord::Interaction

#endif // CONCORD_POINTERINTERACTIONINPUT_H
