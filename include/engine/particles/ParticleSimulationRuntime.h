#ifndef CONCORD_PARTICLESIMULATIONRUNTIME_H
#define CONCORD_PARTICLESIMULATIONRUNTIME_H

#include "Concord/CExport.h"

#include <atomic>
#include <cstdint>

namespace Concord::Particles {

/** Render-device availability observed by particle emitters. */
enum class GpuParticleAvailability : std::uint8_t {
    Unknown = 0,
    Available,
    Unavailable,
};

/** Shares render-thread GPU particle readiness without exposing bgfx state. */
class CENGINE_API ParticleSimulationRuntime {
public:
    /** Returns the last readiness result published by the render backend. */
    static GpuParticleAvailability GpuAvailability() noexcept;

    /** Publishes a render-thread readiness result to scene simulation. */
    static void SetGpuAvailability(GpuParticleAvailability availability) noexcept;

private:
    static std::atomic<GpuParticleAvailability> s_gpuAvailability;
};

} // namespace Concord::Particles

#endif // CONCORD_PARTICLESIMULATIONRUNTIME_H
