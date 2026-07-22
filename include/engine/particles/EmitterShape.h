#ifndef CONCORD_EMITTERSHAPE_H
#define CONCORD_EMITTERSHAPE_H

#include <cstdint>

namespace Concord::Particles {

/**
 * Where new particles are born relative to the emitter's origin.
 *
 * Point spawns every particle at the emitter itself; Sphere/Box/Disc/Cone
 * distribute the spawn positions across a volume or surface so the effect
 * has instant volume (a puff, a wall of sparks, a fountain ring). The
 * emitter's Transform still places the whole spawn region in the world.
 */
enum class EmitterShape : std::uint8_t {
    /** All particles spawn at the emitter origin (a spark, a torch flame). */
    Point = 0,

    /** Spawn uniformly inside a ball of radius `shapeSize.x`. */
    Sphere = 1,

    /** Spawn uniformly inside a box of half-extents `shapeSize`. */
    Box = 2,

    /** Spawn on a horizontal disc of radius `shapeSize.x` in the local XZ plane. */
    Disc = 3,

    /**
     * Spawn along a cone of half-angle `shapeAngleDegrees` opening along
     * `direction`; useful for jets and fountains.
     */
    Cone = 4,
};

} // namespace Concord::Particles

#endif // CONCORD_EMITTERSHAPE_H
