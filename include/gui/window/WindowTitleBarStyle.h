#ifndef CONCORD_WINDOWTITLEBARSTYLE_H
#define CONCORD_WINDOWTITLEBARSTYLE_H

#include "Concord/CExport.h"
#include "engine/ui/UiStyle.h"

#include <string>

namespace Concord::Gui {

/**
 * @brief Visual and behavioral settings for a custom window title bar.
 */
struct CGUI_API WindowTitleBarStyle {
    UI::Color background = UI::Rgba(18, 20, 26, 255);
    UI::Color bottomBorder = UI::Rgba(44, 48, 58, 255);
    UI::Color title = UI::Rgba(235, 238, 245, 255);
    float height = 52.0f;
    float horizontalPadding = 16.0f;
    float iconSize = 24.0f;
    float titleOffsetY = 16.0f;
    float titleGap = 12.0f;
    float buttonWidth = 46.0f;
    float buttonHeight = 36.0f;
    float buttonGap = 6.0f;
    float buttonTop = 8.0f;
    float iconPadding = 10.0f;
    float dragThreshold = 3.0f;
    float doubleClickMs = 400.0f;
    std::string minimizeText = "-";
    std::string maximizeText = "+";
    std::string restoreText = "=";
    std::string fullscreenText = "F";
    std::string windowedText = "W";
    std::string closeText = "X";
    std::string minimizeIcon = "Assets/window_minimize.svg";
    std::string maximizeIcon = "Assets/window_maximize.svg";
    std::string restoreIcon = "Assets/window_restore.svg";
    std::string closeIcon = "Assets/window_close.svg";
    UI::ButtonStyle button;
    UI::ButtonStyle closeButton;
};

} // namespace Concord::Gui

#endif // CONCORD_WINDOWTITLEBARSTYLE_H
