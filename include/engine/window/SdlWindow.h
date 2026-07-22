#ifndef CONCORD_SDLWINDOW_H
#define CONCORD_SDLWINDOW_H

#include "engine/window/WindowMode.h"

#include <string>

struct SDL_Window;
union SDL_Event;

namespace Concord {

/**
 * RAII wrapper around a single SDL3 OS window.
 *
 * Engine-internal: every method must be called from one and the same thread
 * (the engine's render thread), because a window's OS message queue is
 * pumped on the thread that created it. The public Concord::Window spec is
 * what application code uses; this type is the concrete window behind it.
 *
 * The SDL video subsystem itself is owned and pumped centrally by whichever
 * component drives multiple windows (see EngineLoop) rather than by each
 * SdlWindow, since SDL_PollEvent draws from one process-wide event queue
 * shared by every window; this type only opens/closes its own OS window and
 * reacts to the events that name it.
 */
class SdlWindow {
public:
    SdlWindow() = default;
    ~SdlWindow();

    SdlWindow(const SdlWindow&) = delete;
    SdlWindow& operator=(const SdlWindow&) = delete;

    /**
     * Opens the OS window; returns false (and stays closed) on failure.
     * @param visible Pass false for a window that must exist (e.g. to hand
     *        a real native handle to a graphics API) but should never be
     *        shown to the user.
     */
    bool Open(const std::string& title, int width, int height, bool visible = true, bool resizable = false);

    /** Destroys the OS window if open; safe to call when already closed. */
    void Close();

    bool IsOpen() const noexcept { return m_window != nullptr; }

    /** Changes the title of an already-open window; a no-op otherwise. */
    void SetTitle(const std::string& title);

    /** Resizes the client area of an already-open window; a no-op otherwise. */
    void SetSize(int width, int height);

    /** Switches an already-open window between windowed, fullscreen and borderless; a no-op otherwise. */
    void SetMode(WindowMode mode);

    /** Toggles whether the user can resize an already-open window; a no-op otherwise. */
    void SetResizable(bool resizable);

    /** Shows or hides an already-open window; a no-op otherwise. */
    void SetVisible(bool visible);

    /**
     * Reports and clears a pending OS resize. Returns true (with the new pixel
     * size) once per resize event; the loop uses it to rebuild the render target.
     */
    bool TakeResize(int& outWidth, int& outHeight) noexcept;

    /** Returns the current framebuffer size in physical pixels. */
    bool PixelSize(int& outWidth, int& outHeight) const noexcept;

    /** Native window handle (HWND on Windows) for the render backend, or null. */
    void* NativeHandle() const;

    /** Underlying SDL window pointer for event routing, or null when closed. */
    SDL_Window* Handle() const noexcept { return m_window; }

    /**
     * Enables or disables relative mouse mode for this window: SDL hides and
     * locks the cursor to the window and keeps delivering relative motion
     * (xrel/yrel), which is exactly what a first-person / free-look camera
     * needs. A no-op if the window is not open.
     */
    void SetRelativeMouseMode(bool enabled);

    /** Returns the last physical relative-mode result observed from SDL. */
    bool IsRelativeMouseMode() const noexcept { return m_relativeMouseMode; }

    /** Refreshes physical relative-mode state from SDL. */
    void RefreshRelativeMouseMode() noexcept;

    /** Enables or disables recapturing relative mouse mode on a mouse click. */
    void SetClickToRecapture(bool enabled) noexcept;

    /** Feeds one already-polled event to this window; ignored if it names a different window. */
    void HandleEvent(const SDL_Event& event);

    /** Marks this window as needing to close, e.g. on a process-wide SDL_EVENT_QUIT. */
    void RequestClose() noexcept { m_closeRequested = true; }

    /** True once the OS/user (or RequestClose) has asked this window to close. */
    bool CloseRequested() const noexcept { return m_closeRequested; }

private:
    SDL_Window* m_window = nullptr;
    bool m_closeRequested = false;
    bool m_resized = false;
    int m_resizeWidth = 0;
    int m_resizeHeight = 0;
    int m_pixelWidth = 0;
    int m_pixelHeight = 0;
    bool m_captureRequested = false;
    bool m_relativeMouseMode = false;
    bool m_clickToRecapture = true;
    bool m_recaptureArmed = false;
    bool m_restoreCaptureOnFocus = false;
};

} // namespace Concord

#endif // CONCORD_SDLWINDOW_H
