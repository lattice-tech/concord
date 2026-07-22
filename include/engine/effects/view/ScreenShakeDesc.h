#ifndef CONCORD_SCREENSHAKEDESC_H
#define CONCORD_SCREENSHAKEDESC_H

#include "math/Vector2.h"

#include <cstdint>

namespace Concord::Effects {

/**
 * @brief Authoring parameters for one deterministic screen-space shake.
 *
 * Screen shakes offset the resolved image in pixels. They do not modify the
 * camera's world transform, so gameplay queries, shadows and reflections keep
 * using the stable camera pose.
 */
struct ScreenShakeDesc {
    /** Maximum horizontal and vertical displacement in output pixels. */
    Vector2 amplitudePixels{6.0f, 6.0f};

    /** Total lifetime in seconds. Values at or below zero do not start a shake. */
    float duration = 0.35f;

    /** Number of deterministic noise samples per second. */
    float frequency = 28.0f;

    /** Envelope exponent: zero holds full strength; one decays linearly. */
    float decay = 1.5f;

    /** Seed selecting the repeatable noise sequence. */
    std::uint32_t seed = 0x7A11CE5Du;
};

} // namespace Concord::Effects

#endif // CONCORD_SCREENSHAKEDESC_H
