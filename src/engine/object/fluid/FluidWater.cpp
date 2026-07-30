#include "engine/object/fluid/FluidWater.h"

#include "math/Affine.h"

#include <algorithm>
#include <cstring>

namespace Concord::Object {

FluidWater::FluidWater(Fluid::FluidTankDesc desc)
    : m_desc(Fluid::NormalizeFluidDesc(desc)),
      m_layout(Fluid::ComputeFluidLayout(desc))
{
    SetPosition(desc.transform.position);
    SetRotation(desc.transform.rotation);
    SetScale(desc.transform.scale);
    OnUpdate([this](float deltaTime) { Advance(deltaTime); });
}

void FluidWater::SetAbsorption(float absorption) noexcept
{
    if (std::isfinite(absorption)) {
        m_desc.absorption = std::clamp(absorption, 0.0f, 40.0f);
    }
}

void FluidWater::SetIor(float ior) noexcept
{
    if (std::isfinite(ior)) {
        m_desc.ior = std::clamp(ior, 1.0f, 2.5f);
    }
}

void FluidWater::Advance(float deltaTime)
{
    if (m_desc.paused || !std::isfinite(deltaTime) || deltaTime <= 0.0f) {
        return;
    }
    // Clamped so a hitched frame cannot request an unbounded catch-up; the
    // GPU substeps a fixed count regardless, so stability is preserved.
    m_pendingDelta = std::min(m_pendingDelta + deltaTime, 1.0f / 15.0f);
}

void FluidWater::CollectFluids(std::vector<RenderFluid>& out) const
{
    out.push_back(ResolveFluid());
}

RenderFluid FluidWater::ResolveFluid() const
{
    RenderFluid fluid;
    fluid.fluidKey = reinterpret_cast<std::uintptr_t>(this);
    std::memcpy(fluid.world, WorldMatrix(), sizeof(fluid.world));
    if (!AffineInvert(fluid.world, fluid.worldInverse)) {
        std::memcpy(fluid.worldInverse, fluid.world, sizeof(fluid.world));
    }

    fluid.tankSize[0] = m_desc.tankSize.x;
    fluid.tankSize[1] = m_desc.tankSize.y;
    fluid.tankSize[2] = m_desc.tankSize.z;
    fluid.spacing = m_layout.effectiveSpacing;
    fluid.kernelRadius = m_layout.kernelRadius;
    fluid.restDensity = m_desc.restDensity;
    fluid.gravity[1] = -9.81f * m_desc.gravityScale;
    fluid.deltaTime = m_pendingDelta;
    m_pendingDelta = 0.0f;

    fluid.particleCount = m_layout.particleCount;
    fluid.boundaryCount = m_layout.boundaryCount;
    fluid.substeps = m_desc.substeps;
    fluid.densityIterations = m_desc.densityIterations;
    fluid.divergenceIterations = m_desc.divergenceIterations;
    fluid.viscosity = m_desc.viscosity;

    for (int axis = 0; axis < 3; ++axis) {
        fluid.gridDims[axis] = m_layout.gridDims[axis];
        fluid.gridOrigin[axis] = m_layout.gridOrigin[axis];
        fluid.fieldDims[axis] = m_layout.fieldDims[axis];
        fluid.fieldOrigin[axis] = m_layout.fieldOrigin[axis];
        fluid.fluidLattice[axis] = m_layout.fluidLattice[axis];
    }
    fluid.fieldCell = m_layout.fieldCell;
    fluid.fieldSmoothing = m_desc.fieldSmoothing;
    fluid.isoLevel = m_desc.isoLevel;

    fluid.ior = m_desc.ior;
    fluid.absorption = m_desc.absorption;
    fluid.waterColor = m_desc.waterColor;
    fluid.glintStrength = m_desc.glintStrength;
    fluid.roughness = m_desc.roughness;

    const float sizes[3] = {m_desc.tankSize.x, m_desc.tankSize.y, m_desc.tankSize.z};
    const float fillLo[3] = {m_desc.fillMin.x, m_desc.fillMin.y, m_desc.fillMin.z};
    for (int axis = 0; axis < 3; ++axis) {
        // Particles sit at cell centres of the fill lattice.
        fluid.fillOrigin[axis] = (fillLo[axis] - 0.5f) * sizes[axis]
            + 0.5f * m_layout.effectiveSpacing;
    }

    fluid.reset = m_resetPending;
    m_resetPending = false;
    fluid.paused = m_desc.paused;

    if (m_desc.obstacleEnabled) {
        fluid.obstacleMin[0] = m_desc.obstacleMin.x;
        fluid.obstacleMin[1] = m_desc.obstacleMin.y;
        fluid.obstacleMin[2] = m_desc.obstacleMin.z;
        fluid.obstacleMax[0] = m_desc.obstacleMax.x;
        fluid.obstacleMax[1] = m_desc.obstacleMax.y;
        fluid.obstacleMax[2] = m_desc.obstacleMax.z;
        fluid.obstacleBoundaryCount = m_layout.obstacleBoundaryCount;
    }
    return fluid;
}

} // namespace Concord::Object
