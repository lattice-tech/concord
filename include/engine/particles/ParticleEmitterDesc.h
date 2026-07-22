#ifndef CONCORD_PARTICLEEMITTERDESC_H
#define CONCORD_PARTICLEEMITTERDESC_H

#include "color/Color.h"
#include "engine/material/BlendMode.h"
#include "engine/object/PrimitiveShape.h"
#include "engine/object/Transform.h"
#include "engine/particles/EmitterShape.h"
#include "engine/particles/ParticleBurst.h"
#include "engine/particles/ParticleForceField.h"
#include "math/Vector3.h"

#include <cstdint>
#include <limits>
#include <vector>

namespace Concord::Particles {

/**
 * Full configuration of one ParticleEmitter.
 *
 * A plain aggregate: name only the fields you actually vary.
 * ```
 *   scene.Spawn<ParticleEmitter>(ParticleEmitterDesc{
 *       .transform      = {.position = {0.0f, 1.5f, 0.0f}},
 *       .shape          = EmitterShape::Cone,
 *       .shapeAngleDegrees = 25.0f,
 *       .emissionRate   = 40.0f,
 *       .lifetimeMin    = 1.2f, .lifetimeMax = 2.0f,
 *       .speedMin       = 2.5f, .speedMax    = 5.0f,
 *       .gravity        = {0.0f, -6.0f, 0.0f},
 *       .colorStart     = COLOR_RGB(255,220, 90),
 *       .colorMid       = COLOR_RGB(255,120, 30),
 *       .colorEnd       = COLOR_RGB(120, 20, 20),
 *       .bursts         = {{.time = 0.0f, .count = 40}},
 *       .capacity       = 300,
 *   });
 * ```
 *
 * Field groups (in the order they appear below):
 *  - Placement:       Node-relative transform of the whole emitter.
 *  - Emission shape:  where particles spawn (Point / Sphere / Box / Disc / Cone).
 *  - Emission timing: rate, duration, prewarm, discrete bursts, loop.
 *  - Motion:          direction, speed range, spread cone, gravity, drag,
 *                     rotation velocity range, turbulence.
 *  - Force fields:    attractors and vortices applied per frame; ground-plane
 *                     collision with bounce/friction; hard speed cap.
 *  - Appearance:      3-key color and size curves (start / mid / end),
 *                     primitive shape, capacity, lit/unlit.
 *  - Simulation:      simulate positions in local (moves with emitter) or world
 *                     space; deterministic RNG seed.
 */
struct ParticleEmitterDesc {
    // ---- Placement ------------------------------------------------------
    Transform transform{};

    // ---- Emission shape -------------------------------------------------
    EmitterShape shape = EmitterShape::Point;
    /** For Sphere: radius = shapeSize.x. For Box: half-extents. For Disc: radius = x. */
    Vector3 shapeSize{1.0f, 1.0f, 1.0f};
    /** Cone half-angle (degrees); ignored for other shapes. */
    float shapeAngleDegrees = 30.0f;

    // ---- Emission timing ------------------------------------------------
    /** Continuous emission rate, in particles/second. 0 disables the stream. */
    float emissionRate = 30.0f;

    /**
     * How long the emitter emits before it either stops (`loop=false`) or
     * restarts (`loop=true`, resetting scheduled bursts). 0 = infinite when
     * looping, no continuous emission when not.
     */
    float duration = 0.0f;

    /** Discrete bursts, added on top of the continuous rate. */
    std::vector<ParticleBurst> bursts;

    /** True to restart the emitter's clock (and re-fire bursts) after duration. */
    bool loop = true;

    /**
     * True to pre-populate the emitter on first tick with `duration` seconds
     * of continuous emission already advanced — no visible spin-up. Ignored
     * when duration == 0.
     */
    bool prewarm = false;

    // ---- Particle lifetime ----------------------------------------------
    float lifetimeMin = 1.5f;
    float lifetimeMax = 1.5f;

    // ---- Motion ---------------------------------------------------------
    /** Local direction each particle is aimed along (before per-particle spread). */
    Vector3 direction{0.0f, 1.0f, 0.0f};

    /** Uniform random range for the initial speed along the (spread-modulated) direction. */
    float speedMin = 3.0f;
    float speedMax = 3.0f;

    /** Random cone half-angle in degrees around `direction`. Zero = laser-straight. */
    float spreadDegrees = 15.0f;

    /** Constant world-space acceleration (gravity, wind, buoyancy...). */
    Vector3 gravity{0.0f, -3.0f, 0.0f};

    /** Fractional velocity damping per second in [0,1]. */
    float drag = 0.0f;

    /** Random Euler-angle rate range (deg/sec), per particle, applied each frame. */
    Vector3 rotationVelocityMin{};
    Vector3 rotationVelocityMax{};

    /**
     * Deterministic sinusoidal turbulence: injects a small oscillating velocity
     * so particles wander. Zero = perfectly ballistic; a small value (~1) reads
     * as heat haze / wind.
     */
    float turbulenceStrength = 0.0f;

    /** Spatial frequency of the turbulence noise (higher = smaller eddies). */
    float turbulenceFrequency = 1.0f;

    // ---- Force fields & collision ---------------------------------------
    /**
     * Spatial force fields applied each frame, on top of gravity. World-space
     * only (ignored when `localSpace` is true). Empty by default.
     */
    std::vector<ParticleForceField> forceFields;

    /**
     * World-space Y of an infinite ground plane particles bounce off. A NaN
     * (the default) disables collision so the emitter is free-flying.
     */
    float groundY = std::numeric_limits<float>::quiet_NaN();

    /** Restitution in [0,1]: fraction of the downward velocity kept after a bounce. */
    float bounce = 0.3f;

    /** Tangential velocity fraction retained as a particle slides after a bounce. */
    float groundFriction = 0.8f;

    /** Hard speed cap; velocities above this are clamped each frame. <=0 disables. */
    float maxSpeed = 0.0f;

    /**
     * Fraction in [0,1] of the emitter's own velocity to inject into each new
     * particle's initial speed. 0 = particles ignore emitter motion (pure
     * ballistic). 1 = new particles inherit the full emitter velocity, so a
     * moving emitter drags a tail. Rotation alone does not move the emitter
     * origin and therefore contributes no inherited velocity. 0.5 is a useful
     * starting point for trails.
     */
    float inheritEmitterVelocity = 0.0f;

    // ---- Appearance -----------------------------------------------------
    /** 3-key linear color curve (0xRRGGBBAA), sampled at t=0.0 / 0.5 / 1.0. */
    std::uint32_t colorStart = 0xffffffffu;
    std::uint32_t colorMid = 0xffffffffu;
    std::uint32_t colorEnd = 0xffffff00u;

    /** 3-key size curve (world units), sampled the same way as color. */
    float sizeStart = 0.15f;
    float sizeMid = 0.08f;
    float sizeEnd = 0.02f;

    /** Built-in primitive used when `billboard` is false. */
    Object::PrimitiveShape primitiveShape = Object::PrimitiveShape::Sphere;

    /**
     * True to render each particle as an unlit camera-facing quad with a
     * procedural soft core, annular ring and broad halo. Billboard particles
     * do not cast directional shadows.
     */
    bool billboard = true;

    /** Hard cap on simultaneously alive particles; excess emissions are dropped. */
    std::uint32_t capacity = 256;

    /** True for unlit spark/flame look, false to shade with scene lights. */
    bool unlit = true;

    /**
     * How particles composite into the scene. Additive (the default) makes
     * them glow: source alpha weights emitted energy before it is added, so the
     * color curve can fade sparks without losing order independence. Alpha is
     * for soft translucent puffs (smoke, dust) that fade out via the color
     * curve's alpha. Opaque falls back to solid shaded primitives.
     */
    Material::BlendMode blend = Material::BlendMode::Additive;

    /**
     * Total HDR brightness of an unlit particle. Values above 1 add emissive
     * energy so it blooms; values at or below 1 retain the authored base color
     * without adding a duplicate emissive copy. Ignored for lit particles.
     */
    float brightness = 2.0f;

    // ---- Simulation -----------------------------------------------------
    /**
     * True to advance particles in the emitter's local space (so they follow
     * a moving emitter). World-space gravity is rotated into this simulation
     * space; world-space force fields and ground collision are ignored. False
     * (default) bakes spawn positions into world space.
     */
    bool localSpace = false;

    /** RNG seed; two emitters with the same seed evolve identically. */
    std::uint32_t seed = 0xA5F3B21Cu;
};

} // namespace Concord::Particles

#endif // CONCORD_PARTICLEEMITTERDESC_H
