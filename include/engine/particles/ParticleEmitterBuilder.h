#ifndef CONCORD_PARTICLEEMITTERBUILDER_H
#define CONCORD_PARTICLEEMITTERBUILDER_H

#include "color/Color.h"
#include "engine/object/PrimitiveShape.h"
#include "engine/particles/EmitterShape.h"
#include "engine/particles/ParticleBurst.h"
#include "engine/particles/ParticleEmitter.h"
#include "engine/particles/ParticleEmitterDesc.h"
#include "engine/particles/ParticleForceField.h"
#include "engine/scene/Scene.h"
#include "math/Vector3.h"

#include <cstdint>

namespace Concord::Particles {

/**
 * Fluent builder for ParticleEmitterDesc, so scene setup reads as a sentence
 * instead of a 25-field designated initializer:
 *
 *     Particles::Emitter(scene)
 *         .At(5.0f, 0.5f, 5.5f).Cone(25.0f)
 *         .Rate(60).Lifetime(1.6f).Speed(4.0f).Spread(20.0f)
 *         .Gravity({0.0f, -6.0f, 0.0f}).Drag(0.4f)
 *         .Color(COLOR_RGB(255,220,90), COLOR_RGB(255,150,60), COLOR_RGB(200,30,20))
 *         .Size(0.18f, 0.02f).Capacity(200).Unlit().Loop()
 *         .Spawn();
 *
 * Every setter returns *this. Fields not mentioned keep their
 * ParticleEmitterDesc defaults. The builder owns the descriptor and moves it
 * into the spawned node on Spawn(), so it is single-use.
 */
class ParticleEmitterBuilder {
public:
    explicit ParticleEmitterBuilder(Scene& scene) noexcept
        : m_scene(scene)
    {
    }

    // ---- Placement ------------------------------------------------------
    ParticleEmitterBuilder& At(Vector3 pos) noexcept { m_desc.transform.position = pos; return *this; }
    ParticleEmitterBuilder& At(float x, float y, float z) noexcept { m_desc.transform.position = {x, y, z}; return *this; }

    // ---- Emission shape -------------------------------------------------
    ParticleEmitterBuilder& Point() noexcept { m_desc.shape = EmitterShape::Point; return *this; }
    ParticleEmitterBuilder& Sphere(float radius) noexcept
    {
        m_desc.shape = EmitterShape::Sphere;
        m_desc.shapeSize = {radius, radius, radius};
        return *this;
    }
    ParticleEmitterBuilder& Box(Vector3 halfExtents) noexcept { m_desc.shape = EmitterShape::Box; m_desc.shapeSize = halfExtents; return *this; }
    ParticleEmitterBuilder& Disc(float radius) noexcept { m_desc.shape = EmitterShape::Disc; m_desc.shapeSize = {radius, 0.0f, radius}; return *this; }
    ParticleEmitterBuilder& Cone(float halfAngleDegrees) noexcept { m_desc.shape = EmitterShape::Cone; m_desc.shapeAngleDegrees = halfAngleDegrees; return *this; }

    // ---- Emission timing ------------------------------------------------
    ParticleEmitterBuilder& Rate(float particlesPerSecond) noexcept { m_desc.emissionRate = particlesPerSecond; return *this; }
    ParticleEmitterBuilder& Lifetime(float seconds) noexcept { m_desc.lifetimeMin = m_desc.lifetimeMax = seconds; return *this; }
    ParticleEmitterBuilder& Lifetime(float minSeconds, float maxSeconds) noexcept { m_desc.lifetimeMin = minSeconds; m_desc.lifetimeMax = maxSeconds; return *this; }
    ParticleEmitterBuilder& Duration(float seconds) noexcept { m_desc.duration = seconds; return *this; }
    ParticleEmitterBuilder& Loop(bool on = true) noexcept { m_desc.loop = on; return *this; }
    ParticleEmitterBuilder& Once() noexcept { m_desc.loop = false; return *this; }
    ParticleEmitterBuilder& Prewarm(bool on = true) noexcept { m_desc.prewarm = on; return *this; }
    /** Schedules a one-off burst at @p seconds into the emitter's clock. */
    ParticleEmitterBuilder& Burst(float seconds, std::uint32_t count) noexcept { m_desc.bursts.push_back({seconds, count}); return *this; }

    // ---- Motion ---------------------------------------------------------
    ParticleEmitterBuilder& Direction(Vector3 d) noexcept { m_desc.direction = d; return *this; }
    ParticleEmitterBuilder& Speed(float s) noexcept { m_desc.speedMin = m_desc.speedMax = s; return *this; }
    ParticleEmitterBuilder& Speed(float min, float max) noexcept { m_desc.speedMin = min; m_desc.speedMax = max; return *this; }
    ParticleEmitterBuilder& Spread(float degrees) noexcept { m_desc.spreadDegrees = degrees; return *this; }
    ParticleEmitterBuilder& Gravity(Vector3 g) noexcept { m_desc.gravity = g; return *this; }
    ParticleEmitterBuilder& Drag(float d) noexcept { m_desc.drag = d; return *this; }
    ParticleEmitterBuilder& Turbulence(float strength, float frequency = 1.0f) noexcept
    {
        m_desc.turbulenceStrength = strength;
        m_desc.turbulenceFrequency = frequency;
        return *this;
    }
    ParticleEmitterBuilder& Spin(Vector3 minDegPerSec, Vector3 maxDegPerSec) noexcept
    {
        m_desc.rotationVelocityMin = minDegPerSec;
        m_desc.rotationVelocityMax = maxDegPerSec;
        return *this;
    }
    ParticleEmitterBuilder& MaxSpeed(float cap) noexcept { m_desc.maxSpeed = cap; return *this; }
    /**
     * Fraction in [0,1] of the emitter's own velocity injected into each new
     * particle (0 = pure ballistic; 0.5 trails; 1 = full inheritance). The
     * emitter tracks its world-space origin speed each tick; non-zero values
     * make a moving emitter drag a tail.
     */
    ParticleEmitterBuilder& InheritVelocity(float fraction) noexcept { m_desc.inheritEmitterVelocity = fraction; return *this; }

    // ---- Force fields & collision --------------------------------------
    ParticleEmitterBuilder& Attractor(Vector3 pos, float strength, float radius) noexcept
    {
        m_desc.forceFields.push_back({ParticleForceField::Type::Attractor, pos, strength, radius});
        return *this;
    }
    ParticleEmitterBuilder& Repeller(Vector3 pos, float strength, float radius) noexcept
    {
        m_desc.forceFields.push_back({ParticleForceField::Type::Attractor, pos, -strength, radius});
        return *this;
    }
    ParticleEmitterBuilder& Vortex(Vector3 pos, float strength, float radius) noexcept
    {
        m_desc.forceFields.push_back({ParticleForceField::Type::Vortex, pos, strength, radius});
        return *this;
    }
    /** Enables an infinite ground plane at @p y for particles to bounce off. */
    ParticleEmitterBuilder& Ground(float y, float bounce = 0.3f, float friction = 0.8f) noexcept
    {
        m_desc.groundY = y;
        m_desc.bounce = bounce;
        m_desc.groundFriction = friction;
        return *this;
    }

    // ---- Appearance -----------------------------------------------------
    /** 3-key color curve (start / mid / end), packed 0xRRGGBBAA. */
    ParticleEmitterBuilder& Color(std::uint32_t start, std::uint32_t mid, std::uint32_t end) noexcept
    {
        m_desc.colorStart = start; m_desc.colorMid = mid; m_desc.colorEnd = end; return *this;
    }
    /** 2-key color curve (start -> end); the midpoint is set to `start`. */
    ParticleEmitterBuilder& Color(std::uint32_t start, std::uint32_t end) noexcept
    {
        m_desc.colorStart = start; m_desc.colorMid = start; m_desc.colorEnd = end; return *this;
    }
    ParticleEmitterBuilder& Size(float start, float end) noexcept { m_desc.sizeStart = start; m_desc.sizeMid = (start + end) * 0.5f; m_desc.sizeEnd = end; return *this; }
    ParticleEmitterBuilder& Size(float start, float mid, float end) noexcept { m_desc.sizeStart = start; m_desc.sizeMid = mid; m_desc.sizeEnd = end; return *this; }
    /** Uses a built-in mesh instead of the procedural billboard path. */
    ParticleEmitterBuilder& Shape(Object::PrimitiveShape s) noexcept
    {
        m_desc.primitiveShape = s;
        m_desc.billboard = false;
        return *this;
    }
    /** Enables or disables camera-facing procedural halo billboards. */
    ParticleEmitterBuilder& Billboard(bool on = true) noexcept { m_desc.billboard = on; return *this; }
    ParticleEmitterBuilder& Capacity(std::uint32_t c) noexcept { m_desc.capacity = c; return *this; }
    ParticleEmitterBuilder& Unlit(bool on = true) noexcept { m_desc.unlit = on; return *this; }
    ParticleEmitterBuilder& Lit() noexcept { m_desc.unlit = false; return *this; }

    /** Composite mode: Additive (glow, default), Alpha (soft), or Opaque (solid). */
    ParticleEmitterBuilder& Blend(Material::BlendMode mode) noexcept { m_desc.blend = mode; return *this; }
    /** Shorthand for additive glow — sparks, fire, magic. */
    ParticleEmitterBuilder& Additive() noexcept { m_desc.blend = Material::BlendMode::Additive; return *this; }
    /** Shorthand for soft alpha translucency — smoke, dust, clouds. */
    ParticleEmitterBuilder& AlphaBlend() noexcept { m_desc.blend = Material::BlendMode::Alpha; return *this; }
    /** Solid, depth-writing particles (no blending). */
    ParticleEmitterBuilder& Solid() noexcept { m_desc.blend = Material::BlendMode::Opaque; return *this; }
    /** Total unlit brightness; 1 is the authored color and >1 drives bloom. */
    ParticleEmitterBuilder& Brightness(float multiplier) noexcept { m_desc.brightness = multiplier; return *this; }

    // ---- Simulation -----------------------------------------------------
    ParticleEmitterBuilder& LocalSpace(bool on = true) noexcept { m_desc.localSpace = on; return *this; }
    ParticleEmitterBuilder& Seed(std::uint32_t s) noexcept { m_desc.seed = s; return *this; }

    /**
     * Spawns the configured emitter into the scene and returns a reference to
     * it. The builder's descriptor is moved in, so do not call Spawn() twice.
     */
    ParticleEmitter& Spawn() { return m_scene.Spawn<ParticleEmitter>(std::move(m_desc)); }

private:
    Scene& m_scene;
    ParticleEmitterDesc m_desc{};
};

/**
 * Entry point for the fluent builder. Returns a temporary builder that owns a
 * fresh descriptor; chain setters, then call Spawn().
 */
inline ParticleEmitterBuilder Emitter(Scene& scene) noexcept
{
    return ParticleEmitterBuilder{scene};
}

/**
 * Preset factories: common effects one call away. Each returns the spawned
 * emitter so the caller can still Pause/Burst/Restart it afterwards.
 */

/** Upward spark fountain: additive HDR sparks with gravity and drag that glow
 *  and bloom (the showcase demo's gold plume). */
inline ParticleEmitter& Fountain(Scene& scene, Vector3 pos)
{
    return Emitter(scene)
        .At(pos).Sphere(0.08f).Rate(90.0f).Lifetime(1.0f, 1.8f)
        .Direction({0.0f, 1.0f, 0.0f}).Speed(4.0f, 7.5f).Spread(18.0f)
        .Gravity({0.0f, -7.5f, 0.0f}).Drag(0.35f)
        .Turbulence(1.4f, 2.4f)
        .Color(COLOR_RGBA(255, 245, 200, 255), COLOR_RGBA(255, 140, 40, 180),
               COLOR_RGBA(180, 20, 10, 0))
        .Size(0.14f, 0.08f, 0.01f).Capacity(280).Unlit().Additive().Brightness(2.6f).Loop()
        .Spawn();
}

/** One-shot radial explosion: a single burst of fast, fading additive sparks
 *  that flash hot then bloom out. */
inline ParticleEmitter& Explosion(Scene& scene, Vector3 pos, std::uint32_t count = 96)
{
    return Emitter(scene)
        .At(pos).Point()
        .Lifetime(0.8f).Speed(5.0f, 9.0f).Spread(180.0f)
        .Drag(1.5f).Gravity({0.0f, -2.0f, 0.0f})
        .Color(COLOR_RGBA(255, 245, 200, 240), COLOR_RGBA(255, 130, 45, 150),
               COLOR_RGBA(120, 25, 10, 0))
        .Size(0.18f, 0.0f).Capacity(count).Unlit().Additive().Brightness(2.2f)
        .Once().Burst(0.0f, count)
        .Spawn();
}

/** Rising smoke plume: slow, broad, alpha-blended and darkening, prewarmed so
 *  it starts mid-rise. Alpha (not additive): dark smoke must occlude, not glow. */
inline ParticleEmitter& Smoke(Scene& scene, Vector3 pos)
{
    return Emitter(scene)
        .At(pos).Sphere(0.3f).Rate(24.0f).Duration(4.0f).Lifetime(2.5f, 4.0f)
        .Direction({0.0f, 1.0f, 0.0f}).Speed(0.5f, 1.2f).Spread(30.0f)
        .Drag(0.6f).Turbulence(0.4f, 0.8f)
        .Color(COLOR_RGBA(70, 70, 74, 200), COLOR_RGBA(45, 45, 48, 140), COLOR_RGBA(25, 25, 28, 0))
        .Size(0.2f, 0.6f).Capacity(140).Unlit().AlphaBlend().Brightness(0.0f)
        .Loop().Prewarm()
        .Spawn();
}

/** Tall, flickering flame: additive HDR core, upward bias + strong curl
 *  turbulence, so the fire glows and blooms like a real flame. */
inline ParticleEmitter& Fire(Scene& scene, Vector3 pos)
{
    return Emitter(scene)
        .At(pos).Sphere(0.15f).Rate(55.0f).Lifetime(0.6f, 1.0f)
        .Direction({0.0f, 1.0f, 0.0f}).Speed(1.5f, 2.5f).Spread(20.0f)
        .Gravity({0.0f, 0.5f, 0.0f}).Drag(0.8f)
        .Turbulence(1.5f, 2.0f)
        .Color(COLOR_RGBA(255, 235, 150, 220), COLOR_RGBA(255, 130, 45, 140),
               COLOR_RGBA(110, 25, 8, 0))
        .Size(0.2f, 0.0f).Capacity(150).Unlit().Additive().Brightness(1.9f)
        .Loop()
        .Spawn();
}

/** Steady downward rain: high rate, near-vertical, fast, faintly translucent
 *  cool streaks (alpha-blended so they read as water, not glowing lines). */
inline ParticleEmitter& Rain(Scene& scene, Vector3 pos, Vector3 area = {10.0f, 0.0f, 10.0f})
{
    return Emitter(scene)
        .At(pos).Box(area).Rate(300.0f).Lifetime(1.2f, 1.6f)
        .Direction({0.0f, -1.0f, 0.0f}).Speed(15.0f, 18.0f).Spread(2.0f)
        .Gravity({0.0f, -2.0f, 0.0f}).Drag(0.0f)
        .Color(COLOR_RGBA(200, 220, 255, 200), COLOR_RGBA(190, 210, 245, 160), COLOR_RGBA(180, 200, 230, 0))
        .Size(0.02f, 0.02f).Capacity(400).Unlit().AlphaBlend().Brightness(0.0f)
        .Loop()
        .Spawn();
}

/** Twinkling sparkles: omnidirectional, very short, hot additive motes with
 *  quick drag — magic dust that pops and blooms. */
inline ParticleEmitter& Sparkle(Scene& scene, Vector3 pos)
{
    return Emitter(scene)
        .At(pos).Point().Rate(20.0f).Lifetime(0.3f, 0.6f)
        .Direction({0.0f, 1.0f, 0.0f}).Speed(0.5f, 1.5f).Spread(180.0f)
        .Gravity({0.0f, 0.0f, 0.0f}).Drag(2.0f)
        .Color(COLOR_RGBA(255, 250, 210, 230), COLOR_RGBA(255, 220, 110, 140),
               COLOR_RGBA(120, 60, 20, 0))
        .Size(0.04f, 0.0f).Capacity(80).Unlit().Additive().Brightness(2.0f)
        .Loop()
        .Spawn();
}

/**
 * Swirling magic orb: a dense ring of coloured additive sparks caught in a
 * vortex around @p pos, drifting inward with curl turbulence. The signature
 * "wow" effect — bright HDR colour that blooms into a glowing halo.
 */
inline ParticleEmitter& MagicOrb(Scene& scene, Vector3 pos,
                                 std::uint32_t startColor = COLOR_RGB(120, 90, 255),
                                 std::uint32_t endColor = COLOR_RGB(60, 200, 255))
{
    // Layered: hot core sparks + outer mist for a denser, more "VFX package" look.
    Emitter(scene)
        .At(pos).Sphere(0.35f).Rate(40.0f).Lifetime(0.5f, 0.9f)
        .Direction({0.0f, 1.0f, 0.0f}).Speed(0.2f, 0.6f).Spread(180.0f)
        .Gravity({0.0f, 0.0f, 0.0f}).Drag(0.8f)
        .Vortex(pos, 8.0f, 2.0f).Attractor(pos, 5.0f, 2.0f)
        .Color(COLOR_RGBA(255, 255, 255, 255), COLOR_RGBA(220, 200, 255, 160),
               COLOR_RGBA(120, 80, 255, 0))
        .Size(0.08f, 0.04f, 0.0f).Capacity(100).Unlit().Additive().Brightness(3.2f)
        .Loop()
        .Spawn();
    return Emitter(scene)
        .At(pos).Sphere(1.1f).Rate(100.0f).Lifetime(1.1f, 1.9f)
        .Direction({0.0f, 1.0f, 0.0f}).Speed(0.5f, 1.4f).Spread(180.0f)
        .Gravity({0.0f, 0.15f, 0.0f}).Drag(0.45f)
        .Turbulence(1.6f, 2.6f)
        .Vortex(pos, 7.5f, 3.5f).Attractor(pos, 3.5f, 3.5f)
        .Color((startColor & 0xffffff00u) | 220u, COLOR_RGBA(180, 140, 255, 140),
               endColor & 0xffffff00u)
        .Size(0.06f, 0.1f, 0.0f).Capacity(260).Unlit().Additive().Brightness(2.4f)
        .Loop()
        .Spawn();
}

/**
 * Energy portal: a thin, fast, tall column of additive sparks spraying up out
 * of a disc, with strong curl turbulence — reads as a crackling beam/portal.
 */
inline ParticleEmitter& Portal(Scene& scene, Vector3 pos,
                               std::uint32_t color = COLOR_RGB(80, 255, 180))
{
    return Emitter(scene)
        .At(pos).Disc(0.7f).Rate(180.0f).Lifetime(0.8f, 1.4f)
        .Direction({0.0f, 1.0f, 0.0f}).Speed(4.0f, 7.0f).Spread(8.0f)
        .Gravity({0.0f, -1.0f, 0.0f}).Drag(0.3f)
        .Turbulence(1.6f, 2.6f)
        .Color(COLOR_RGBA(220, 255, 240, 220), (color & 0xffffff00u) | 140u,
               COLOR_RGBA(20, 80, 60, 0))
        .Size(0.07f, 0.01f).Capacity(260).Unlit().Additive().Brightness(1.9f)
        .Loop()
        .Spawn();
}

} // namespace Concord::Particles

#endif // CONCORD_PARTICLEEMITTERBUILDER_H
