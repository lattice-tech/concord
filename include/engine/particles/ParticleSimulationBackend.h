#ifndef CONCORD_PARTICLESIMULATIONBACKEND_H
#define CONCORD_PARTICLESIMULATIONBACKEND_H

#include <cstdint>

namespace Concord::Particles {

/** Selects where one emitter advances its per-particle state. */
enum class ParticleSimulationBackend : std::uint8_t {
    Cpu = 0,
    Gpu = 1,
};

/** Maximum persistent particles supported by one GPU emitter. */
inline constexpr std::uint32_t kMaxGpuParticleCapacity = 65'536;

} // namespace Concord::Particles

#endif // CONCORD_PARTICLESIMULATIONBACKEND_H
