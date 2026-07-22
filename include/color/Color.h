#ifndef CONCORD_COLOR_H
#define CONCORD_COLOR_H

/**
 * Named color constants for the packed RGBA values the engine uses.
 *
 * Every drawable color in Concord is a std::uint32_t in 0xRRGGBBAA order
 * (see Object::BoxDesc::color). These macros put a readable name on the
 * common ones so call sites read `.color = COLOR_RED` instead of a raw hex
 * literal. Build any other color with COLOR_RGBA / COLOR_RGB, whose
 * components are plain 0-255 bytes.
 *
 * These are macros (not namespaced constants) on purpose: the request was
 * for a `COLOR_RED`-style vocabulary usable verbatim at any call site, and
 * an integer literal carries no type to qualify. The public entry point is
 * <Concord/CColor.h>, which forwards here.
 */

/** Builds a packed 0xRRGGBBAA color from four 0-255 components. */
#define COLOR_RGBA(r, g, b, a)                       \
    ((((unsigned)(r) & 0xFFu) << 24)                 \
     | (((unsigned)(g) & 0xFFu) << 16)               \
     | (((unsigned)(b) & 0xFFu) << 8)                \
     | ((unsigned)(a) & 0xFFu))

/** Builds an opaque packed color from three 0-255 components (alpha = 255). */
#define COLOR_RGB(r, g, b) COLOR_RGBA(r, g, b, 255)

#define COLOR_WHITE      0xFFFFFFFFu
#define COLOR_BLACK      0x000000FFu
#define COLOR_RED        0xFF0000FFu
#define COLOR_GREEN      0x00FF00FFu
#define COLOR_BLUE       0x0000FFFFu
#define COLOR_YELLOW     0xFFFF00FFu
#define COLOR_CYAN       0x00FFFFFFu
#define COLOR_MAGENTA    0xFF00FFFFu
#define COLOR_ORANGE     0xFF7F00FFu
#define COLOR_PURPLE     0x7F00FFFFu
#define COLOR_PINK       0xFF7FBFFFu
#define COLOR_BROWN      0x8B4513FFu
#define COLOR_GRAY       0x808080FFu
#define COLOR_LIGHT_GRAY 0xC0C0C0FFu
#define COLOR_DARK_GRAY  0x404040FFu

/** Fully transparent (alpha = 0). */
#define COLOR_TRANSPARENT 0x00000000u

#endif // CONCORD_COLOR_H
