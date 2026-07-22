#ifndef CONCORD_UIINPUT_H
#define CONCORD_UIINPUT_H

namespace Concord::UI {

/**
 * One frame of pointer state driving widget interaction, in screen pixels
 * (top-left origin). The caller fills this from the engine input each frame on
 * the simulation thread before building the UI. Keeping input as plain data
 * passed in (rather than the UI reaching into the input system) keeps the UI
 * core decoupled and testable.
 *
 * The pressed/released edges must be single-frame: true only on the frame the
 * primary button transitioned, so Button() can track a press-then-release.
 */
struct Input {
    float pointerX = 0.0f;
    float pointerY = 0.0f;
    bool pointerDown = false;     ///< Primary button currently held.
    bool pointerPressed = false;  ///< Primary button went down this frame (edge).
    bool pointerReleased = false; ///< Primary button went up this frame (edge).
};

} // namespace Concord::UI

#endif // CONCORD_UIINPUT_H
