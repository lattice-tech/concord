#ifndef CONCORD_UIDRAWLIST_H
#define CONCORD_UIDRAWLIST_H

#include "engine/ui/UiTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Concord::UI {

/** What a single draw command paints. */
enum class DrawKind : std::uint8_t {
    SolidRect,    ///< Filled rectangle (rect + color).
    StyledRect,   ///< Filled/bordered rectangle with optional rounded corners.
    Text,         ///< Text laid out inside rect with alignment.
    TexturedRect, ///< Image quad (rect + texture + color tint); Phase 3.
};

/**
 * One backend-agnostic UI paint command, in screen pixels (top-left origin).
 *
 * This is deliberately plain data so a whole DrawList can cross the thread
 * boundary to the render thread unchanged. Text carries only its bounding rect
 * and alignment - the renderer, which owns the font atlas and glyph metrics,
 * performs the final text layout. The UI core therefore needs no font metrics.
 */
struct DrawCommand {
    DrawKind kind = DrawKind::SolidRect;
    Rect rect;
    Color color = Rgba(255, 255, 255, 255);

    std::string text;              ///< Payload for DrawKind::Text.
    Align hAlign = Align::Start;   ///< Text horizontal alignment within rect.
    Align vAlign = Align::Center;  ///< Text vertical alignment within rect.
    float fontScale = 1.0f;

    std::uint32_t texture = 0;     ///< Engine TextureId handle; 0 = none.
    Color borderColor = Rgba(0, 0, 0, 0);
    float borderThickness = 0.0f;
    float cornerRadius = 0.0f;
};

/** An ordered list of paint commands produced by one UI frame. */
struct DrawList {
    std::vector<DrawCommand> commands;

    void Clear() noexcept { commands.clear(); }
    bool Empty() const noexcept { return commands.empty(); }
};

} // namespace Concord::UI

#endif // CONCORD_UIDRAWLIST_H
