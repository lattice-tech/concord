#include "engine/particles/ParticleSimulationRuntime.h"

namespace Concord::Particles {

std::atomic<GpuParticleAvailability> ParticleSimulationRuntime::s_gpuAvailability{
    GpuParticleAvailability::Unknown};

GpuParticleAvailability ParticleSimulationRuntime::GpuAvailability() noexcept
{
    return s_gpuAvailability.load(std::memory_order_acquire);
}

void ParticleSimulationRuntime::SetGpuAvailability(
    GpuParticleAvailability availability) noexcept
{
    s_gpuAvailability.store(availability, std::memory_order_release);
}

} // namespace Concord::Particles
