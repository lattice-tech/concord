#ifndef CONCORD_PARTICLEEMITTER_H
#define CONCORD_PARTICLEEMITTER_H

#include "Concord/CExport.h"
#include "engine/object/Node.h"
#include "engine/particles/ParticleEmitterDesc.h"
#include "engine/render/frame/RenderInstance.h"
#include "math/Vector3.h"

#include <cstdint>
#include <vector>

namespace Concord::Particles {

/**
 * CPU particle system: a Scene node that emits, integrates and draws a fixed
 * pool of short-lived particles per its ParticleEmitterDesc.
 *
 * Runtime capabilities (see ParticleEmitterDesc for authoring):
 *   - Emission shapes: point / sphere / box / disc / cone
 *   - Continuous rate + scheduled bursts + one-shot mode + looping duration
 *   - Prewarm (fast-forward on the first tick so it starts "already going")
 *   - Randomised lifetime, speed, direction (spread cone), rotation velocity
 *   - Gravity, drag, cheap deterministic turbulence
 *   - 3-key color and size curves over particle lifetime
 *   - Local-space (follows a moving emitter) or world-space simulation
 *   - Deterministic per-emitter RNG seed for reproducible runs
 *
 * Spawn like any other node:
 * ```
 *   auto& fx = scene.Spawn<ParticleEmitter>(ParticleEmitterDesc{...});
 *   fx.Burst(64);       // extra one-off puff on top of the continuous stream
 *   fx.Pause();         // freeze emission; alive particles keep evolving
 * ```
 * Persists into `.cscene` (kind = ParticleEmitter): the descriptor is stored,
 * live particles are not (they are transient runtime state).
 */
class CENGINE_API ParticleEmitter : public Object::Node {
public:
    explicit ParticleEmitter(ParticleEmitterDesc desc = {});

    const ParticleEmitterDesc& Desc() const noexcept { return m_desc; }

    /** Runtime knobs after construction — safe from any thread the Scene is on. */
    void SetEmissionRate(float rate) noexcept { m_desc.emissionRate = rate; }
    void Pause() noexcept { m_paused = true; }
    void Resume() noexcept { m_paused = false; }

    /** Restarts the emitter's clock; re-fires scheduled bursts. */
    void Restart();

    /** One-off burst on top of the continuous stream. Bounded by remaining capacity. */
    void Burst(std::uint32_t count);

    /** Number of currently alive particles. */
    std::uint32_t Alive() const noexcept { return m_alive; }

private:
    struct Particle {
        Vector3 position{};       ///< world (or local, see localSpace) position
        Vector3 velocity{};
        Vector3 rotation{};       ///< Euler degrees
        Vector3 angularVelocity{}; ///< Euler degrees/second
        float age = 0.0f;
        float lifetime = 0.0f;
        std::uint32_t seed = 0;   ///< per-particle for turbulence phase
        bool alive = false;
    };

    /** Per-frame tick: integrate + spawn + fire bursts. Called via Node::OnUpdate. */
    void Advance(float deltaTime);

    /** Renderable hook: one instance per alive particle. */
    void CollectRender(std::vector<RenderInstance>& out) const override;

    /** Spawns one particle into the first free slot. `emitterWorld` is the current world matrix. */
    void SpawnOne(const float emitterWorld[16]);

    /** Fires any bursts whose scheduled time has passed since the last tick. */
    void FireDueBursts(float previousElapsed, float currentElapsed, const float emitterWorld[16]);

    /** Advances the simulation by @p seconds without emitting (used by prewarm). */
    void FastForward(float seconds);

    /** One sub-step of the simulation: integrate, then emit (continuous + bursts when allowed). */
    void Step(float dt, bool allowBursts);

    /** Ages and moves live particles by @p dt; does not spawn or emit. */
    void Integrate(float dt);

    ParticleEmitterDesc m_desc;
    std::vector<Particle> m_pool;
    std::uint32_t m_alive = 0;
    float m_emissionAccumulator = 0.0f;
    float m_elapsed = 0.0f;             ///< seconds since the emitter's clock started
    std::uint32_t m_rngState = 0;
    bool m_paused = false;
    bool m_prewarmed = false;

    /**
     * Last observed emitter world position and implied velocity; used by
     * `inheritEmitterVelocity` to drag a tail behind a moving / rotating
     * emitter. Updated each Advance from the live world matrix.
     */
    Vector3 m_lastEmitterPos{};
    Vector3 m_emitterSpeed{};
    bool m_emitterPosInit = false;
};

} // namespace Concord::Particles

#endif // CONCORD_PARTICLEEMITTER_H
