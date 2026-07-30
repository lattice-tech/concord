#ifndef CONCORD_WINDOWCHROMECONFIG_H
#define CONCORD_WINDOWCHROMECONFIG_H

#include <array>
#include <cstdint>

namespace Concord {

/**
 * @brief Borderless window hit-test configuration for custom chrome.
 */
struct WindowChromeRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

/**
 * @brief Borderless window hit-test configuration for custom chrome.
 */
struct WindowChromeConfig {
    bool enabled = false;
    float titleBarHeight = 0.0f;
    float resizeBorder = 0.0f;
    float captionLeftInset = 0.0f;
    float captionRightInset = 0.0f;
    std::array<WindowChromeRect, 8> captionExcludeRects{};
    std::uint32_t captionExcludeRectCount = 0;
};

} // namespace Concord

#endif // CONCORD_WINDOWCHROMECONFIG_H
