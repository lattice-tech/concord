#ifndef CONCORD_WINDOWTITLEBAR_H
#define CONCORD_WINDOWTITLEBAR_H

#include "Concord/CExport.h"
#include "engine/ui/UiContext.h"
#include "engine/ui/UiTypes.h"
#include "engine/window/WindowMode.h"
#include "gui/window/WindowTitleBarStyle.h"

#include <chrono>
#include <string>

namespace Concord {
class Window;
}

namespace Concord::Gui {

/**
 * @brief Immediate-mode custom title bar with window controls and drag handling.
 */
class CGUI_API WindowTitleBar {
public:
    void SetStyle(const WindowTitleBarStyle& style);
    const WindowTitleBarStyle& GetStyle() const noexcept { return m_style; }

    void SetLogoPath(std::string logoPath);
    const std::string& LogoPath() const noexcept { return m_logoPath; }

    float Height() const noexcept;
    void Draw(UI::Context& ui, const UI::Input& input, Window& window);

private:
    void DrawIconButton(UI::Context& ui, std::uint32_t id, const UI::Rect& rect,
                        const std::string& iconPath, const UI::ButtonStyle& style,
                        Window& window, void (WindowTitleBar::*action)(Window&));
    void ToggleMaximize(Window& window);
    void ToggleFullscreen(Window& window);
    void MinimizeWindow(Window& window);
    void CloseWindow(Window& window);
    static UI::Rect MakeButtonRect(const WindowTitleBarStyle& style, float windowWidth,
                                   std::uint32_t indexFromRight) noexcept;

    WindowTitleBarStyle m_style;
    std::string m_logoPath = "Assets/Concordw.png";
    WindowMode m_restoreMode = WindowMode::Borderless;
};

} // namespace Concord::Gui

#endif // CONCORD_WINDOWTITLEBAR_H
