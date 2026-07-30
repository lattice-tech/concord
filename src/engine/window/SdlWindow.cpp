#include "engine/window/SdlWindow.h"

#include "engine/debug/Logger.h"

#include <SDL3/SDL.h>

namespace Concord {

namespace {

bool PointInChromeRect(const WindowChromeRect& rect, int x, int y) noexcept
{
    if (rect.width <= 0.0f || rect.height <= 0.0f) {
        return false;
    }
    return static_cast<float>(x) >= rect.x
        && static_cast<float>(y) >= rect.y
        && static_cast<float>(x) < rect.x + rect.width
        && static_cast<float>(y) < rect.y + rect.height;
}

SDL_HitTestResult SDLCALL HitTestCallback(SDL_Window* rawWindow, const SDL_Point* area,
                                          void* data)
{
    (void)rawWindow;
    if (area == nullptr || data == nullptr) {
        return SDL_HITTEST_NORMAL;
    }
    const SdlWindow* window = static_cast<const SdlWindow*>(data);
    const WindowChromeConfig& chrome = window->Chrome();
    if (!chrome.enabled || chrome.resizeBorder <= 0.0f) {
        return SDL_HITTEST_NORMAL;
    }

    int width = 0;
    int height = 0;
    if (!window->PixelSize(width, height) || width <= 0 || height <= 0) {
        return SDL_HITTEST_NORMAL;
    }

    const int x = area->x;
    const int y = area->y;
    const int border = static_cast<int>(chrome.resizeBorder);
    const bool left = x < border;
    const bool right = x >= width - border;
    const bool top = y < border;
    const bool bottom = y >= height - border;

    if (top && left) return SDL_HITTEST_RESIZE_TOPLEFT;
    if (top && right) return SDL_HITTEST_RESIZE_TOPRIGHT;
    if (bottom && left) return SDL_HITTEST_RESIZE_BOTTOMLEFT;
    if (bottom && right) return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    if (top) return SDL_HITTEST_RESIZE_TOP;
    if (bottom) return SDL_HITTEST_RESIZE_BOTTOM;
    if (left) return SDL_HITTEST_RESIZE_LEFT;
    if (right) return SDL_HITTEST_RESIZE_RIGHT;

    if (chrome.titleBarHeight > 0.0f
        && y >= 0 && y < static_cast<int>(chrome.titleBarHeight)
        && x >= static_cast<int>(chrome.captionLeftInset)
        && x < width - static_cast<int>(chrome.captionRightInset)) {
        const std::uint32_t excludeCount = std::min(
            chrome.captionExcludeRectCount,
            static_cast<std::uint32_t>(chrome.captionExcludeRects.size()));
        for (std::uint32_t index = 0; index < excludeCount; ++index) {
            if (PointInChromeRect(chrome.captionExcludeRects[index], x, y)) {
                return SDL_HITTEST_NORMAL;
            }
        }
        return SDL_HITTEST_DRAGGABLE;
    }

    return SDL_HITTEST_NORMAL;
}

SDL_HitTestResult ToHitTest(WindowResizeEdge edge) noexcept
{
    switch (edge) {
        case WindowResizeEdge::Left: return SDL_HITTEST_RESIZE_LEFT;
        case WindowResizeEdge::Top: return SDL_HITTEST_RESIZE_TOP;
        case WindowResizeEdge::Right: return SDL_HITTEST_RESIZE_RIGHT;
        case WindowResizeEdge::Bottom: return SDL_HITTEST_RESIZE_BOTTOM;
        case WindowResizeEdge::TopLeft: return SDL_HITTEST_RESIZE_TOPLEFT;
        case WindowResizeEdge::TopRight: return SDL_HITTEST_RESIZE_TOPRIGHT;
        case WindowResizeEdge::BottomLeft: return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        case WindowResizeEdge::BottomRight: return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
    }
    return SDL_HITTEST_NORMAL;
}

} // namespace

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

void SdlWindow::Minimize()
{
    if (m_window) {
        SDL_MinimizeWindow(m_window);
    }
}

void SdlWindow::Maximize()
{
    if (m_window) {
        SDL_MaximizeWindow(m_window);
    }
}

void SdlWindow::Restore()
{
    if (m_window) {
        SDL_RestoreWindow(m_window);
    }
}

void SdlWindow::BeginDrag(float pointerX, float pointerY)
{
    if (!m_window) {
        return;
    }
    int windowX = 0;
    int windowY = 0;
    if (!SDL_GetWindowPosition(m_window, &windowX, &windowY)) {
        return;
    }
    m_dragging = true;
    m_dragAnchorX = static_cast<int>(pointerX) + windowX;
    m_dragAnchorY = static_cast<int>(pointerY) + windowY;
}

void SdlWindow::BeginResize(WindowResizeEdge edge)
{
    if (!m_window) {
        return;
    }
    SDL_SetWindowHitTest(m_window, nullptr, nullptr);
    (void)ToHitTest(edge);
    SDL_SetWindowHitTest(m_window, HitTestCallback, this);
}

void SdlWindow::SetChrome(const WindowChromeConfig& config)
{
    m_chrome = config;
    if (!m_window) {
        return;
    }
    SDL_SetWindowHitTest(m_window, m_chrome.enabled ? HitTestCallback : nullptr,
                         m_chrome.enabled ? this : nullptr);
}

void SdlWindow::UpdateDrag()
{
    if (!m_window || !m_dragging) {
        return;
    }
    float globalX = 0.0f;
    float globalY = 0.0f;
    const SDL_MouseButtonFlags buttons = SDL_GetGlobalMouseState(&globalX, &globalY);
    if ((buttons & SDL_BUTTON_LMASK) == 0) {
        m_dragging = false;
        return;
    }
    const int windowX = static_cast<int>(globalX) - m_dragAnchorX;
    const int windowY = static_cast<int>(globalY) - m_dragAnchorY;
    SDL_SetWindowPosition(m_window, windowX, windowY);
}

bool SdlWindow::IsMinimized() const noexcept
{
    return m_window != nullptr
        && (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED) != 0;
}

bool SdlWindow::IsMaximized() const noexcept
{
    return m_window != nullptr
        && (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED) != 0;
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
    } else if (event.type == SDL_EVENT_WINDOW_MINIMIZED) {
        m_dragging = false;
    } else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        m_dragging = false;
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
