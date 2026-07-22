#ifndef CONCORD_UISTYLE_H
#define CONCORD_UISTYLE_H

#include "engine/ui/UiTypes.h"

namespace Concord::UI {

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
    Color accent       = Rgba(90, 140, 210, 255); ///< Progress fill, highlights.

    float padding   = 8.0f;  ///< Default inner padding, pixels.
    float fontScale = 1.0f;  ///< Multiplies the base font size for UI text.
};

} // namespace Concord::UI

#endif // CONCORD_UISTYLE_H
