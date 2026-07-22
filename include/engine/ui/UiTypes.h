#ifndef CONCORD_UITYPES_H
#define CONCORD_UITYPES_H

#include <cstdint>

namespace Concord::UI {

/**
 * Packed 0xRRGGBBAA color, matching materials / PrintString / debug text so a
 * single packing convention is shared across the engine.
 */
using Color = std::uint32_t;

/** Builds a packed color from 8-bit channels (alpha defaults to opaque). */
constexpr Color Rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                     std::uint8_t a = 255) noexcept
{
    return (static_cast<Color>(r) << 24) | (static_cast<Color>(g) << 16)
         | (static_cast<Color>(b) << 8) | static_cast<Color>(a);
}

/**
 * A screen-space rectangle in pixels, top-left origin, y down. Widget layout
 * and hit-testing operate in this space; the renderer converts to NDC.
 */
struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    /** True when (px, py) lies inside the rectangle (half-open on the far edges). */
    constexpr bool Contains(float px, float py) const noexcept
    {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
};

/** Content alignment within a rect, on one axis. */
enum class Align : std::uint8_t {
    Start,  ///< Left (horizontal) or top (vertical).
    Center,
    End,    ///< Right (horizontal) or bottom (vertical).
};

} // namespace Concord::UI

#endif // CONCORD_UITYPES_H
