#ifndef CONCORD_WINDOWDESC_H
#define CONCORD_WINDOWDESC_H

#include "engine/render/postprocess/AntiAliasing.h"
#include "engine/window/Resolution.h"
#include "engine/window/WindowMode.h"

#include <string>

namespace Concord {

/**
 * Every field a Window can be constructed or Set() from.
 *
 * A plain aggregate so a caller can build one with designated
 * initializers and only name the fields it actually wants to change, e.g.
 * `WindowDesc{.title = "My Game"}`. Every field carries its own default,
 * so the result is always a fully-formed value rather than an incomplete
 * one, regardless of how few fields were named. Set() replaces a Window's
 * description wholesale (the same semantics as construction) — a field
 * left unnamed falls back to *this type's* default, not to whatever the
 * Window's current value happens to be, so a partial update must copy any
 * fields it wants to keep from Window::Desc() first.
 *
 * There is deliberately no icon field here: a window's icon always comes
 * from Concord.cfg's `icon=` key, baked into the exe at build time (see
 * docs/配置.md), so it can never be changed per-window at runtime and
 * has no place in a runtime-facing description like this one.
 */
struct WindowDesc {
    /** Caption shown in the window's title bar. */
    std::string title = "Concord";

    /** Client-area size, in pixels. Ignored while `mode` is Fullscreen. */
    Resolution resolution{};

    /** How the window is presented: windowed, fullscreen or borderless. */
    WindowMode mode = WindowMode::Windowed;

    /**
     * Whether the user can resize the window by dragging its edges. When on,
     * the engine follows OS resizes and rebuilds the render target to match,
     * so the view always fills the window. Ignored while `mode` is Fullscreen.
     */
    bool resizable = true;

    /** Whether the OS window is shown after it is attached. */
    bool visible = true;

    /**
     * Whether presentation waits for the display's vertical refresh. On
     * eliminates tearing and caps the frame rate to the refresh rate; off
     * lets frames present as soon as they are ready. bgfx applies this
     * process-wide, so — like `antialiasing` — the most recently attached or
     * Set() value wins for every window.
     */
    bool vsync = true;

    /**
     * Whether the OS mouse cursor is visible while over an engine window.
     * SDL only exposes this as process-wide state (there is no per-window
     * cursor visibility on the platforms Concord targets), so the most
     * recently attached or Set() window's value wins for every window.
     */
    bool showCursor = true;

    /**
     * Whether the mouse is captured for first-person / free-look control.
     * When true the engine puts the window in relative mouse mode: the OS
     * cursor is hidden and locked to the window, and mouse motion keeps
     * arriving as relative deltas (Input::MouseDeltaX/Y) with no edge clamp —
     * exactly what a WASD + mouse-look camera needs. This overrides
     * `showCursor` while active. Like the other process-wide window state,
     * the most recently attached or Set() value wins across windows. Toggle
     * it off (e.g. on Esc) to give the cursor back for menus.
     */
    bool captureMouse = false;

    /**
     * Whether a mouse click physically restores relative mouse mode after
     * `captureMouse` transitions from true to false. This is useful when a
     * menu temporarily releases a first-person camera: clicking the rendered
     * window resumes mouse capture without synthesizing an input event.
     */
    bool clickToRecapture = true;

    /**
     * Full-scene anti-aliasing technique applied to this window's rendered
     * image. Covers both hardware multisampling (MSAA2/4/8) and post-process
     * passes (FXAA, SMAA2/4); see AntiAliasing. Applied when the window is
     * first attached and whenever Set() changes this field — the render
     * backend rebuilds the window's framebuffer and offscreen targets to
     * match (MSAA modes also reset the process-wide swap chain).
     *
     * Defaults to FXAA, pairing with the HDR offscreen + final dither present
     * path. The config file's `antialiasing=` key overrides this default at
     * attach time (see GameConfig), but a caller can then change it live via
     * `window.Set({.antialiasing = AntiAliasing::Fxaa})` or
     * `window.SetAntialiasing(...)`.
     */
    AntiAliasing antialiasing = AntiAliasing::Fxaa;
};

} // namespace Concord

#endif // CONCORD_WINDOWDESC_H
