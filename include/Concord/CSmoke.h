#ifndef CONCORD_CSMOKE_H
#define CONCORD_CSMOKE_H

/**
 * Public facade for local volumetric smoke (AGENTS.md §3): a scene-node box of
 * participating medium the renderer ray-marches and composites against scene
 * depth. Distinct from `Particles::Smoke` (cheap camera-facing billboards, see
 * CParticles.h) and from the sky's global volumetric clouds. Consumers include
 * only this header and never reach into the engine module directly.
 *
 *     scene.Spawn<Concord::Object::SmokeVolume>(Concord::Gameplay::SmokeVolumeDesc{
 *         .transform = {.position = {0.0f, 2.0f, 0.0f}},
 *         .halfExtents = {3.0f, 2.0f, 3.0f},
 *         .density = 0.8f,
 *     });
 */

#include "engine/object/SmokeVolume.h"
#include "engine/object/SmokeVolumeDesc.h"

#endif // CONCORD_CSMOKE_H
