#ifndef CONCORD_INPUT_H
#define CONCORD_INPUT_H

#include "Concord/CExport.h"
#include "engine/input/Key.h"
#include "engine/input/MouseButton.h"

namespace Concord {

/**
 * Query point for keyboard and mouse input (CEngine.dll).
 *
 * A stateless static facade over the engine's process-wide input state, which
 * the render thread updates every frame from platform events. Intended to be
 * polled from game logic — typically a node's OnUpdate (see Scene) — where
 * "pressed"/"released" mean "changed since the previous frame". Reads are
 * thread-safe; before any Game/window exists everything reads as up/zero.
 */
class CENGINE_API Input {
public:
    Input() = delete;

    /** True while `key` is held down. */
    static bool IsKeyDown(Key key);

    /** True only on the frame `key` went from up to down. */
    static bool WasKeyPressed(Key key);

    /** True only on the frame `key` went from down to up. */
    static bool WasKeyReleased(Key key);

    /** True while `button` is held down. */
    static bool IsMouseButtonDown(MouseButton button);

    /** True only on the frame `button` went from up to down. */
    static bool WasMouseButtonPressed(MouseButton button);

    /** True only on the frame `button` went from down to up. */
    static bool WasMouseButtonReleased(MouseButton button);

    /** Mouse X position in window pixels (origin top-left). */
    static float MouseX();

    /** Mouse Y position in window pixels (origin top-left). */
    static float MouseY();

    /** Mouse X movement accumulated over the current frame, in pixels. */
    static float MouseDeltaX();

    /** Mouse Y movement accumulated over the current frame, in pixels. */
    static float MouseDeltaY();

    /** Mouse wheel movement accumulated over the current frame (+ is up/away). */
    static float MouseWheel();
};

} // namespace Concord

#endif // CONCORD_INPUT_H
