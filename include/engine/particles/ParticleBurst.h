#ifndef CONCORD_PARTICLEBURST_H
#define CONCORD_PARTICLEBURST_H

#include <cstdint>

namespace Concord::Particles {

/**
 * One scheduled particle burst: emit `count` particles at once at `time`
 * seconds after the emitter starts (or after each loop, when the emitter
 * loops). Bursts are additive on top of the continuous `emissionRate` and
 * are the standard way to make explosions, footstep puffs, muzzle flashes.
 */
struct ParticleBurst {
    /** Seconds from the emitter's own start clock at which this burst fires. */
    float time = 0.0f;

    /** How many particles to emit in the burst. */
    std::uint32_t count = 16;
};

} // namespace Concord::Particles

#endif // CONCORD_PARTICLEBURST_H
