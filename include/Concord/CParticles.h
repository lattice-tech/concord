#ifndef CONCORD_CPARTICLES_H
#define CONCORD_CPARTICLES_H

/**
 * Public entry point for the CPU particle system.
 *
 * Re-exports the real declarations from the particles module (facade pattern,
 * AGENTS.md §3): a Scene node that emits, advances and draws a fixed pool of
 * particles, its configuration descriptor, the force-field/collision data
 * types, and a fluent builder plus preset factories for terse scene setup.
 *
 *     // Long form — full control via the descriptor:
 *     auto& fx = scene.Spawn<Concord::Particles::ParticleEmitter>(
 *         Concord::Particles::ParticleEmitterDesc{...});
 *
 *     // Short form — fluent builder:
 *     auto& fx = Concord::Particles::Emitter(scene)
 *                   .At(5.0f, 0.5f, 5.5f).Cone(25.0f)
 *                   .Rate(60).Lifetime(1.6f).Speed(4.0f).Spread(20.0f)
 *                   .Gravity({0.0f, -6.0f, 0.0f}).Drag(0.4f)
 *                   .Color(COLOR_RGB(255,220,90), COLOR_RGB(255,150,60), COLOR_RGB(200,30,20))
 *                   .Size(0.18f, 0.02f).Capacity(200).Unlit().Loop()
 *                   .Spawn();
 *
 *     // Preset — one call:
 *     Concord::Particles::Fountain(scene, {5.0f, 0.5f, 5.5f});
 */
#include "engine/particles/EmitterShape.h"
#include "engine/particles/ParticleBurst.h"
#include "engine/particles/ParticleEmitter.h"
#include "engine/particles/ParticleEmitterBuilder.h"
#include "engine/particles/ParticleEmitterDesc.h"
#include "engine/particles/ParticleForceField.h"

#endif // CONCORD_CPARTICLES_H
