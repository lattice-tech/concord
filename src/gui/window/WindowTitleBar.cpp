#include "gui/window/WindowTitleBar.h"

#include "engine/window/Window.h"

#include <utility>

namespace Concord::Gui {

void WindowTitleBar::SetStyle(const WindowTitleBarStyle& style)
{
    m_style = style;
}

void WindowTitleBar::SetLogoPath(std::string logoPath)
{
    m_logoPath = std::move(logoPath);
}

float WindowTitleBar::Height() const noexcept { return m_style.height; }

UI::Rect WindowTitleBar::MakeButtonRect(const WindowTitleBarStyle& style, float windowWidth,
                                        std::uint32_t indexFromRight) noexcept
{
    const float right = style.horizontalPadding
        + static_cast<float>(indexFromRight) * (style.buttonWidth + style.buttonGap);
    return UI::Rect{
        windowWidth - right - style.buttonWidth,
        style.buttonTop,
        style.buttonWidth,
        style.buttonHeight,
    };
}

void WindowTitleBar::ToggleMaximize(Window& window)
{
    if (window.IsMaximized()) {
        window.Restore();
        return;
    }
    if (window.Mode() != WindowMode::Fullscreen) {
        m_restoreMode = window.Mode();
    }
    window.Maximize();
}

void WindowTitleBar::ToggleFullscreen(Window& window)
{
    if (window.Mode() == WindowMode::Fullscreen) {
        window.SetMode(m_restoreMode);
        return;
    }
    m_restoreMode = window.Mode();
    window.SetMode(WindowMode::Fullscreen);
}

void WindowTitleBar::MinimizeWindow(Window& window)
{
    window.Minimize();
}

void WindowTitleBar::CloseWindow(Window& window)
{
    window.RequestClose();
}

void WindowTitleBar::DrawIconButton(UI::Context& ui, std::uint32_t id, const UI::Rect& rect,
                                    const std::string& iconPath, const UI::ButtonStyle& style,
                                    Window& window, void (WindowTitleBar::*action)(Window&))
{
    if (ui.Button(id, rect, "", style, true)) {
        (this->*action)(window);
    }
    ui.Image(UI::Rect{rect.x + m_style.iconPadding,
                      rect.y + m_style.iconPadding,
                      rect.width - m_style.iconPadding * 2.0f,
                      rect.height - m_style.iconPadding * 2.0f},
             iconPath);
}

void WindowTitleBar::Draw(UI::Context& ui, const UI::Input& input, Window& window)
{
    (void)input;
    const float windowWidth = static_cast<float>(window.Width());
    const UI::Rect barRect{0.0f, 0.0f, windowWidth, m_style.height};
    ui.Panel(barRect, m_style.background);
    ui.Panel(UI::Rect{0.0f, m_style.height - 1.0f, windowWidth, 1.0f}, m_style.bottomBorder);

    ui.Image(UI::Rect{m_style.horizontalPadding,
                      (m_style.height - m_style.iconSize) * 0.5f,
                      m_style.iconSize,
                      m_style.iconSize}, m_logoPath);
    ui.Label(m_style.horizontalPadding + m_style.iconSize + m_style.titleGap,
             m_style.titleOffsetY, window.Title(), m_style.title);

    const UI::Rect closeRect = MakeButtonRect(m_style, windowWidth, 0);
    const UI::Rect fullscreenRect = MakeButtonRect(m_style, windowWidth, 1);
    const UI::Rect maximizeRect = MakeButtonRect(m_style, windowWidth, 2);
    const UI::Rect minimizeRect = MakeButtonRect(m_style, windowWidth, 3);

    DrawIconButton(ui, 0x1002u, minimizeRect, m_style.minimizeIcon, m_style.button,
                   window, &WindowTitleBar::MinimizeWindow);
    if (ui.Button(0x1003u, maximizeRect, "", m_style.button, true)) {
        ToggleMaximize(window);
    }
    ui.Image(UI::Rect{maximizeRect.x + m_style.iconPadding,
                      maximizeRect.y + m_style.iconPadding,
                      maximizeRect.width - m_style.iconPadding * 2.0f,
                      maximizeRect.height - m_style.iconPadding * 2.0f},
             window.IsMaximized() ? m_style.restoreIcon : m_style.maximizeIcon);
    if (ui.Button(0x1004u, fullscreenRect,
                  window.Mode() == WindowMode::Fullscreen
                      ? m_style.windowedText : m_style.fullscreenText,
                  m_style.button, true)) {
        ToggleFullscreen(window);
    }
    DrawIconButton(ui, 0x1005u, closeRect, m_style.closeIcon, m_style.closeButton,
                   window, &WindowTitleBar::CloseWindow);
}

} // namespace Concord::Gui
