#ifndef CONCORD_WINDOWINPUTEVENTS_H
#define CONCORD_WINDOWINPUTEVENTS_H

#include "engine/input/Key.h"
#include "engine/input/MouseButton.h"
#include "engine/window/WindowId.h"

namespace Concord {

/**
 * @brief Keyboard notification for one physical key transition or repeat.
 *
 * Delivered on the EngineLoop thread before Game / Scene update. Does not
 * replace `Input::IsKeyDown` polling for continuous held state.
 */
struct KeyEvent {
    WindowId window = kInvalidWindowId;
    Key key = Key::Unknown;
    bool down = false;
    bool repeat = false;
};

/**
 * @brief Mouse button press or release on a specific window.
 */
struct MouseButtonEvent {
    WindowId window = kInvalidWindowId;
    MouseButton button = MouseButton::Left;
    bool down = false;
};

/**
 * @brief Coalesced mouse motion for one window over a single frame.
 *
 * Multiple SDL motion events for the same window are merged into one event
 * before dispatch: latest position and summed deltas.
 */
struct MouseMotionEvent {
    WindowId window = kInvalidWindowId;
    float x = 0.0f;
    float y = 0.0f;
    float deltaX = 0.0f;
    float deltaY = 0.0f;
};

/**
 * @brief Mouse wheel scroll attributed to one window.
 */
struct MouseWheelEvent {
    WindowId window = kInvalidWindowId;
    float delta = 0.0f;
};

/**
 * @brief Window keyboard / mouse focus gained or lost.
 *
 * On focus loss the engine also releases held keys and buttons that were owned
 * by this window so process-wide polling cannot stick.
 */
struct WindowFocusChangedEvent {
    WindowId window = kInvalidWindowId;
    bool focused = false;
};

/**
 * @brief Final positive framebuffer pixel size after a resize is applied.
 *
 * Width and height always come from `SDL_GetWindowSizeInPixels` (not logical
 * window coordinates). Zero / minimized sizes are not published.
 */
struct WindowResizedEvent {
    WindowId window = kInvalidWindowId;
    int width = 0;
    int height = 0;
};

/**
 * @brief User or OS requested that the window close.
 *
 * Notification only — not vetoable. The OS window is already closed by the
 * time handlers run this frame; `window` is a source token for correlation,
 * not a live handle for further window operations.
 */
struct WindowCloseRequestedEvent {
    WindowId window = kInvalidWindowId;
};

} // namespace Concord

#endif // CONCORD_WINDOWINPUTEVENTS_H
