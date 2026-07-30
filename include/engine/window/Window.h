#ifndef CONCORD_WINDOW_H
#define CONCORD_WINDOW_H

#include "Concord/CExport.h"
#include "engine/loop/EngineLoop.h"
#include "engine/window/WindowDesc.h"
#include "engine/window/WindowChromeConfig.h"
#include "engine/window/WindowId.h"
#include "engine/window/WindowResizeEdge.h"

#include <memory>
#include <string>

namespace Concord {

class Game;

/**
 * Describes a window the application wants the engine to open.
 *
 * Constructing one does not create any OS window: it is only a
 * specification, built from a WindowDesc so a caller can name just the
 * fields it cares about with a designated initializer, e.g.
 * `Window({.title = "My Game"})`. The real platform window is created,
 * driven and destroyed by the engine's render thread once the spec is
 * handed to Game::AttachWindow — that keeps all windowing on a single
 * thread, so it stays responsive while the logic thread runs or sleeps.
 *
 * Once attached, this same object doubles as a live handle: Set() updates
 * its description and, as long as it is still attached, pushes that same
 * change to the already-open OS window on the render thread (see
 * docs/窗口.md). Calling Set() before ever attaching, or after the
 * window has been detached, only updates the local description.
 */
class CENGINE_API Window {
public:
    explicit Window(WindowDesc desc = {});

    /** The full current description (title, resolution, cursor, AA, ...). */
    const WindowDesc& Desc() const noexcept;

    const std::string& Title() const noexcept;
    int Width() const noexcept;
    int Height() const noexcept;
    WindowMode Mode() const noexcept;
    bool Resizable() const noexcept;
    /** Whether the attached OS window is requested to be visible. */
    bool Visible() const noexcept;
    bool Vsync() const noexcept;
    bool ShowCursor() const noexcept;
    bool CaptureMouse() const noexcept;
    /** Whether relative mouse mode is physically active in the attached SDL window. */
    bool IsMouseCaptured() const noexcept;
    /** Whether a click may physically recapture a previously released mouse. */
    bool ClickToRecapture() const noexcept;

    /** The full-scene anti-aliasing technique this window currently uses. */
    AntiAliasing Antialiasing() const noexcept;

    /** Whether the live OS window is currently minimized. */
    bool IsMinimized() const noexcept;

    /** Whether the live OS window is currently maximized. */
    bool IsMaximized() const noexcept;

    /**
     * Replaces this window's description wholesale (see WindowDesc) and,
     * if currently attached, pushes the change to the live OS window;
     * blocks briefly for the render thread to apply it, same as
     * Game::AttachWindow/DetachWindow.
     */
    void Set(WindowDesc desc);

    /**
     * Convenience: changes only the anti-aliasing technique and pushes it
     * live (if attached). Equivalent to copying Desc(), setting
     * `antialiasing`, and calling Set().
     */
    void SetAntialiasing(AntiAliasing aa);

    /** Convenience: changes only the presentation mode and pushes it live. */
    void SetMode(WindowMode mode);

    /** Convenience: shows or hides the attached OS window. */
    void SetVisible(bool visible);

    /** Convenience: changes only the process-wide cursor visibility policy. */
    void SetShowCursor(bool showCursor);

    /** Convenience: enables or releases relative mouse capture. */
    void SetCaptureMouse(bool captureMouse);

    /** Convenience: enables or disables click-to-recapture for this window. */
    void SetClickToRecapture(bool clickToRecapture);

    /** Requests the attached OS window to minimize. */
    void Minimize();

    /** Requests the attached OS window to maximize. */
    void Maximize();

    /** Requests the attached OS window to restore from min/max state. */
    void Restore();

    /** Starts dragging a borderless OS window from one client-area point. */
    void BeginDrag(float pointerX, float pointerY);

    /** Starts resizing a borderless OS window from one edge or corner. */
    void BeginResize(WindowResizeEdge edge);

    /** Requests the attached OS window to close. */
    void RequestClose();

    /** Configures custom title-bar drag and border hit-testing for a borderless window. */
    void SetChrome(WindowChromeConfig config);

    /**
     * Process-unique id assigned when this window is attached, or
     * `kInvalidWindowId` when detached / never attached. Matches
     * `window` fields on typed input and window events.
     */
    WindowId Id() const noexcept { return m_windowId; }

private:
    friend class Game;

    /** Wires this Window up to the loop/id that now owns it, so a later Set() can reach it; called by Game::AttachWindow only. */
    void BindLive(std::weak_ptr<EngineLoop> loop, EngineLoop::WindowId id);

    WindowDesc m_desc;
    std::weak_ptr<EngineLoop> m_loop;
    EngineLoop::WindowId m_windowId = EngineLoop::kInvalidWindowId;
};

} // namespace Concord

#endif // CONCORD_WINDOW_H
