#ifndef CONCORD_RENDERPARTICLEEMITTER_H
#define CONCORD_RENDERPARTICLEEMITTER_H

#include "engine/object/ObjectId.h"
#include "engine/particles/ParticleEmitterDesc.h"
#include "math/Vector3.h"

#include <cstdint>

namespace Concord {

/** Maximum force fields carried by one GPU particle command. */
inline constexpr std::uint32_t kMaxRenderParticleForceFields = 8;

/**
 * @brief Immutable render-thread command for one GPU-simulated emitter.
 *
 * `spawnSequence` and `simulationTime` are cumulative within `resetGeneration`.
 * A renderer uses them to recover work when publication replaces an intermediate
 * snapshot before it is consumed. `spawnCount` and `deltaTime` describe only the
 * latest simulation tick and are useful when snapshots arrive consecutively.
 */
struct RenderParticleEmitter {
    /** Process-local equality key; renderers must never dereference it. */
    std::uintptr_t emitterKey = 0;
    Object::ObjectId emitterId = Object::kInvalidObjectId;
    std::uint32_t resetGeneration = 1;
    std::uint64_t spawnSequence = 0;
    std::uint32_t spawnCount = 0;
    double simulationTime = 0.0;
    float deltaTime = 0.0f;
    float prewarmSeconds = 0.0f;
    float world[16]{};
    Vector3 emitterVelocity{};
    Particles::ParticleEmitterDesc descriptor{};
};

} // namespace Concord

#endif // CONCORD_RENDERPARTICLEEMITTER_H
