#include "engine/window/SdlWindow.h"

#include "engine/debug/Logger.h"

#include <SDL3/SDL.h>

namespace Concord {

SdlWindow::~SdlWindow()
{
    Close();
}

bool SdlWindow::Open(const std::string& title, int width, int height, bool visible, bool resizable)
{
    if (m_window) {
        return true;
    }

    SDL_WindowFlags flags = visible ? 0 : SDL_WINDOW_HIDDEN;
    if (resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    m_window = SDL_CreateWindow(title.c_str(), width, height, flags);
    if (!m_window) {
        Debug::Logger::Error("Window", "SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    if (visible) {
        SDL_ShowWindow(m_window);
    }
    SDL_GetWindowSizeInPixels(m_window, &m_pixelWidth, &m_pixelHeight);
    m_closeRequested = false;
    return true;
}

void SdlWindow::Close()
{
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
}

void SdlWindow::SetTitle(const std::string& title)
{
    if (m_window) {
        SDL_SetWindowTitle(m_window, title.c_str());
    }
}

void SdlWindow::SetSize(int width, int height)
{
    if (m_window) {
        SDL_SetWindowSize(m_window, width, height);
    }
}

void SdlWindow::SetMode(WindowMode mode)
{
    if (!m_window) {
        return;
    }
    switch (mode) {
        case WindowMode::Fullscreen:
            // SDL3 with no explicit fullscreen mode gives desktop (borderless)
            // fullscreen, which covers the display without a video-mode switch.
            SDL_SetWindowFullscreen(m_window, true);
            break;
        case WindowMode::Borderless:
            SDL_SetWindowFullscreen(m_window, false);
            SDL_SetWindowBordered(m_window, false);
            break;
        case WindowMode::Windowed:
        default:
            SDL_SetWindowFullscreen(m_window, false);
            SDL_SetWindowBordered(m_window, true);
            break;
    }
}

void* SdlWindow::NativeHandle() const
{
    if (!m_window) {
        return nullptr;
    }
    const SDL_PropertiesID props = SDL_GetWindowProperties(m_window);
    return SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
}

void SdlWindow::SetResizable(bool resizable)
{
    if (m_window) {
        SDL_SetWindowResizable(m_window, resizable);
    }
}

void SdlWindow::SetVisible(bool visible)
{
    if (!m_window) {
        return;
    }
    if (visible) {
        SDL_ShowWindow(m_window);
    } else {
        SDL_HideWindow(m_window);
    }
}

void SdlWindow::SetRelativeMouseMode(bool enabled)
{
    const bool wasRequested = m_captureRequested;
    m_captureRequested = enabled;
    if (m_window) {
        m_relativeMouseMode = SDL_SetWindowRelativeMouseMode(m_window, enabled) && enabled;
    } else {
        m_relativeMouseMode = false;
    }
    if (enabled) {
        m_recaptureArmed = false;
        m_restoreCaptureOnFocus = false;
    } else if (wasRequested) {
        m_recaptureArmed = m_clickToRecapture;
        m_restoreCaptureOnFocus = false;
    }
}

void SdlWindow::SetClickToRecapture(bool enabled) noexcept
{
    m_clickToRecapture = enabled;
    if (!enabled) {
        m_recaptureArmed = false;
    }
}

void SdlWindow::RefreshRelativeMouseMode() noexcept
{
    m_relativeMouseMode = m_window && SDL_GetWindowRelativeMouseMode(m_window);
}

bool SdlWindow::TakeResize(int& outWidth, int& outHeight) noexcept
{
    int pixelWidth = 0;
    int pixelHeight = 0;
    if (m_window && SDL_GetWindowSizeInPixels(m_window, &pixelWidth, &pixelHeight)
        && pixelWidth > 0 && pixelHeight > 0
        && (pixelWidth != m_pixelWidth || pixelHeight != m_pixelHeight)) {
        m_pixelWidth = pixelWidth;
        m_pixelHeight = pixelHeight;
        m_resized = true;
        m_resizeWidth = pixelWidth;
        m_resizeHeight = pixelHeight;
    }
    if (!m_resized) {
        return false;
    }
    outWidth = m_resizeWidth;
    outHeight = m_resizeHeight;
    m_resized = false;
    return true;
}

void SdlWindow::HandleEvent(const SDL_Event& event)
{
    if (!m_window || SDL_GetWindowFromEvent(&event) != m_window) {
        return;
    }
    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        m_closeRequested = true;
    } else if (event.type == SDL_EVENT_WINDOW_RESIZED
               || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED
               || event.type == SDL_EVENT_WINDOW_MAXIMIZED
               || event.type == SDL_EVENT_WINDOW_RESTORED
               || event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN
               || event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) {
        // Always query physical pixels: RESIZED data is in window coordinates
        // and can differ on high-DPI displays. TakeResize also polls this value
        // each frame, covering backends that omit one of these notifications.
        int pixelWidth = 0;
        int pixelHeight = 0;
        if (SDL_GetWindowSizeInPixels(m_window, &pixelWidth, &pixelHeight)
            && pixelWidth > 0 && pixelHeight > 0) {
            m_pixelWidth = pixelWidth;
            m_pixelHeight = pixelHeight;
            m_resized = true;
            m_resizeWidth = pixelWidth;
            m_resizeHeight = pixelHeight;
        }
    } else if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        if (m_captureRequested || m_restoreCaptureOnFocus) {
            m_relativeMouseMode = SDL_SetWindowRelativeMouseMode(m_window, true);
        }
        m_restoreCaptureOnFocus = false;
    } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
        // SDL releases relative mode on focus loss; requested mode is retried on gain.
        m_restoreCaptureOnFocus = m_relativeMouseMode;
        m_relativeMouseMode = false;
    } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
               m_clickToRecapture && m_recaptureArmed && !m_captureRequested) {
        m_relativeMouseMode = SDL_SetWindowRelativeMouseMode(m_window, true);
        m_recaptureArmed = false;
    }
}

bool SdlWindow::PixelSize(int& outWidth, int& outHeight) const noexcept
{
    outWidth = 0;
    outHeight = 0;
    return m_window && SDL_GetWindowSizeInPixels(m_window, &outWidth, &outHeight)
        && outWidth > 0 && outHeight > 0;
}

} // namespace Concord
