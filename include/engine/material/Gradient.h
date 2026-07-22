#ifndef CONCORD_GRADIENT_H
#define CONCORD_GRADIENT_H

#include "color/Color.h"
#include "engine/material/GradientAxis.h"

#include <cstdint>

namespace Concord::Material {

/**
 * A two-color linear gradient painted across a surface's base color.
 *
 * When `enabled`, the surface's albedo is replaced by a blend from `from`
 * (at the negative end of `axis`) to `to` (at the positive end), evaluated
 * per pixel in the primitive's local space. The gradient is orthogonal to
 * the lighting model: enabling it under MaterialModel::Lit still lets the
 * blended color be lit, while under Unlit it shows through as-is.
 *
 * A plain aggregate so a caller only names what it wants, e.g.
 * `.gradient = {.enabled = true, .from = COLOR_RED, .to = COLOR_BLUE}`.
 */
struct Gradient {
    /** Whether the gradient participates; when false the other fields are ignored. */
    bool enabled = false;

    /** Packed 0xRRGGBBAA color at the negative end of `axis`. */
    std::uint32_t from = COLOR_BLACK;

    /** Packed 0xRRGGBBAA color at the positive end of `axis`. */
    std::uint32_t to = COLOR_WHITE;

    /** Local-space direction the blend runs along. */
    GradientAxis axis = GradientAxis::Y;
};

} // namespace Concord::Material

#endif // CONCORD_GRADIENT_H
