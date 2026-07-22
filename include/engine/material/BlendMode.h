#ifndef CONCORD_BLENDMODE_H
#define CONCORD_BLENDMODE_H

#include <cstdint>

namespace Concord::Material {

/**
 * How a surface's shaded color combines with what is already in the frame
 * buffer.
 *
 * Opaque is ordinary depth-sorted geometry: the fragment replaces whatever
 * was there and writes depth. Alpha and Additive are the transparency modes
 * behind particles, glows and glass; the render backend draws every blended
 * surface after all opaque geometry and with depth writes disabled, so
 * translucent draws still test against solid geometry but never occlude each
 * other through the depth buffer.
 *
 * Additive is order-independent (light only ever accumulates), which is why
 * it is the workhorse for sparks, fire and magic — no per-frame back-to-front
 * sort is needed for it to look right.
 */
enum class BlendMode : std::uint8_t {
    /** Source replaces destination; the default for solid geometry. */
    Opaque = 0,

    /** src*srcAlpha + dst*(1-srcAlpha): ordinary translucency (smoke, glass). */
    Alpha = 1,

    /** src*srcAlpha + dst: alpha-weighted light accumulation for fading glows. */
    Additive = 2,
};

} // namespace Concord::Material

#endif // CONCORD_BLENDMODE_H
