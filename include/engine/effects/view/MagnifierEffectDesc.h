#ifndef CONCORD_MAGNIFIEREFFECTDESC_H
#define CONCORD_MAGNIFIEREFFECTDESC_H

#include "math/Vector2.h"

namespace Concord::Effects {

/**
 * @brief Configuration for a circular screen-space magnifier and lens warp.
 *
 * Coordinates are resolution independent. The backend interprets radius and
 * feather relative to the shorter viewport dimension so the lens remains
 * circular on non-square windows.
 */
struct MagnifierEffectDesc {
    /** Whether the magnifier contributes to the current view. */
    bool enabled = false;

    /** Lens center in normalized viewport coordinates; (0, 0) is bottom-left. */
    Vector2 center{0.5f, 0.5f};

    /** Lens radius as a fraction of the shorter viewport dimension. */
    float radius = 0.25f;

    /** Magnification factor. One leaves the source image unchanged. */
    float zoom = 2.0f;

    /** Signed radial warp strength; positive and negative values bend oppositely. */
    float distortion = 0.0f;

    /** Soft transition width at the lens edge, in the same units as radius. */
    float feather = 0.03f;
};

} // namespace Concord::Effects

#endif // CONCORD_MAGNIFIEREFFECTDESC_H
