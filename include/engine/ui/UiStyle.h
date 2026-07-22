#ifndef CONCORD_UISTYLE_H
#define CONCORD_UISTYLE_H

#include "engine/ui/UiTypes.h"

namespace Concord::UI {

/** @brief Complete visual override for one button instance. */
struct ButtonStyle {
    Color fill = Rgba(48, 54, 66, 255);
    Color hover = Rgba(66, 74, 90, 255);
    Color active = Rgba(90, 120, 170, 255);
    Color disabled = Rgba(42, 44, 50, 210);
    Color text = Rgba(235, 238, 245, 255);
    Color textDisabled = Rgba(145, 148, 156, 255);
    Color focus = Rgba(255, 214, 92, 255);
    float fontScale = 1.0f;
    float focusThickness = 2.0f;
};

/**
 * Colors and metrics shared by the immediate-mode widgets. A game can override
 * any field to reskin its HUD without touching widget code. Defaults are a
 * neutral dark theme.
 */
struct Style {
    Color panel        = Rgba(24, 26, 32, 220);  ///< Panel / window background.
    Color text         = Rgba(235, 238, 245, 255);
    Color button       = Rgba(48, 54, 66, 255);   ///< Idle button fill.
    Color buttonHover  = Rgba(66, 74, 90, 255);
    Color buttonActive = Rgba(90, 120, 170, 255); ///< While pressed.
    Color buttonDisabled = Rgba(42, 44, 50, 210);
    Color textDisabled = Rgba(145, 148, 156, 255);
    Color focus        = Rgba(255, 214, 92, 255); ///< Keyboard-focus outline.
    Color accent       = Rgba(90, 140, 210, 255); ///< Progress fill, highlights.

    float padding        = 8.0f;  ///< Default inner padding, pixels.
    float fontScale      = 1.0f;  ///< Multiplies the base font size for UI text.
    float focusThickness = 2.0f;  ///< Visible focus-outline width in pixels.
};

/** @brief Creates a per-button style matching a context-wide theme. */
inline ButtonStyle ButtonStyleFrom(const Style& style) noexcept
{
    return ButtonStyle{style.button, style.buttonHover, style.buttonActive,
                       style.buttonDisabled, style.text, style.textDisabled,
                       style.focus, style.fontScale, style.focusThickness};
}

} // namespace Concord::UI

#endif // CONCORD_UISTYLE_H
