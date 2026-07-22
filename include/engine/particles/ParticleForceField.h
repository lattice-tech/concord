#ifndef CONCORD_PARTICLEFORCEFIELD_H
#define CONCORD_PARTICLEFORCEFIELD_H

#include "math/Vector3.h"

#include <cstdint>

namespace Concord::Particles {

/**
 * A spatial force applied to every alive particle each frame, on top of the
 * emitter's uniform gravity. The base gravity/drag/turbulence knobs describe a
 * purely ballistic field; force fields add the localized pulls and swirls that
 * make a particle effect read as "high-end": an orbiting vortex, a magnetic
 * snap to a moving point, a repulsion blast away from an explosion center.
 *
 * Positions are world-space, so force fields only affect emitters simulating
 * in world space (`ParticleEmitterDesc::localSpace == false`); a local-space
 * emitter ignores them (its particles live in the emitter's own frame).
 */
struct ParticleForceField {
    enum class Type : std::uint8_t {
        /** Pulls particles toward `position` (strength > 0) or pushes away (< 0). */
        Attractor = 0,
        /** Swirls particles around `position` about the world up axis (Y). */
        Vortex = 1,
    };

    /** Which kind of field this is. */
    Type type = Type::Attractor;

    /** World-space anchor the field acts from. */
    Vector3 position{};

    /**
     * Force magnitude. Attractor: acceleration (world units/sec²) toward (or,
     * when negative, away from) `position`. Vortex: tangential acceleration
     * around `position`.
     */
    float strength = 5.0f;

    /**
     * Beyond this distance the field has no effect, with a linear falloff from
     * full strength at `position` to zero at `radius`. <= 0 means infinite
     * reach with no falloff.
     */
    float radius = 10.0f;
};

} // namespace Concord::Particles

#endif // CONCORD_PARTICLEFORCEFIELD_H
