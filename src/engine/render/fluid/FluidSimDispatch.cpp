#include "engine/render/fluid/BgfxFluidRenderer.h"

#include "engine/render/fluid/FluidProgramSlots.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>

namespace {

float UIntAsFloat(std::uint32_t value) noexcept
{
    return std::bit_cast<float>(value);
}

std::uint32_t DispatchGroups(std::uint32_t threads,
                             std::uint32_t groupSize) noexcept
{
    return std::max<std::uint32_t>(1u, (threads + groupSize - 1u) / groupSize);
}

std::uint32_t FieldVertexCount(const Concord::RenderFluid& fluid) noexcept
{
    return (fluid.fieldDims[0] + 1u) * (fluid.fieldDims[1] + 1u)
        * (fluid.fieldDims[2] + 1u);
}

/**
 * Bisection gate for the reconstruction chain, read once from
 * CONCORD_FLUID_RECON_STAGE: 0 = off, 1 = density splat, 2 = + temporal
 * smoothing, 3 = + sparse MC classify, 4 = + MC emission (full chain).
 * The field/MC chain once hard-froze the GPU; while the root cause is being
 * pinned down the stage can be lowered to find the dispatch that brings the
 * freeze back. Defaults to the full chain.
 */
int ReconstructionStage()
{
    static const int stage = [] {
        const char* value = std::getenv("CONCORD_FLUID_RECON_STAGE");
        if (value == nullptr || value[0] == '\0') {
            return 4;
        }
        return std::clamp(std::atoi(value), 0, 4);
    }();
    return stage;
}

} // namespace

namespace Concord {

void BgfxFluidRenderer::BindParams(const RenderFluid& fluid, float dt,
                                   std::uint32_t flags, float maxSpeed) const
{
    const float spacing = std::max(fluid.spacing, 1.0e-4f);
    const float kernelRadius = std::max(fluid.kernelRadius, spacing);
    const float restDensity = std::max(fluid.restDensity, 1.0f);
    const float particleMass = spacing * spacing * spacing * restDensity;
    const float tankMin[3] = {
        -0.5f * fluid.tankSize[0], -0.5f * fluid.tankSize[1], -0.5f * fluid.tankSize[2]};
    const float tankMax[3] = {
         0.5f * fluid.tankSize[0],  0.5f * fluid.tankSize[1],  0.5f * fluid.tankSize[2]};
    const float localGravity[3] = {
        fluid.worldInverse[0] * fluid.gravity[0]
            + fluid.worldInverse[4] * fluid.gravity[1]
            + fluid.worldInverse[8] * fluid.gravity[2],
        fluid.worldInverse[1] * fluid.gravity[0]
            + fluid.worldInverse[5] * fluid.gravity[1]
            + fluid.worldInverse[9] * fluid.gravity[2],
        fluid.worldInverse[2] * fluid.gravity[0]
            + fluid.worldInverse[6] * fluid.gravity[1]
            + fluid.worldInverse[10] * fluid.gravity[2],
    };
    const std::uint32_t wallX = std::max<std::uint32_t>(
        2u, static_cast<std::uint32_t>(std::floor(fluid.tankSize[0] / spacing)));
    const std::uint32_t wallY = std::max<std::uint32_t>(
        2u, static_cast<std::uint32_t>(std::floor(fluid.tankSize[1] / spacing)));
    const std::uint32_t wallZ = std::max<std::uint32_t>(
        2u, static_cast<std::uint32_t>(std::floor(fluid.tankSize[2] / spacing)));
    const float fieldFixedScale = 65536.0f;
    const float fieldFixedInvScale = 1.0f / fieldFixedScale;
    const float params[16][4] = {
        {dt, dt * dt, UIntAsFloat(fluid.particleCount),
         UIntAsFloat(fluid.particleCount + fluid.boundaryCount)},
        {spacing, kernelRadius, restDensity, particleMass},
        {localGravity[0], localGravity[1], localGravity[2], fluid.viscosity},
        {fluid.gridOrigin[0], fluid.gridOrigin[1], fluid.gridOrigin[2], kernelRadius},
        {UIntAsFloat(fluid.gridDims[0]), UIntAsFloat(fluid.gridDims[1]),
         UIntAsFloat(fluid.gridDims[2]), UIntAsFloat(flags)},
        {tankMin[0], tankMin[1], tankMin[2], maxSpeed},
        {tankMax[0], tankMax[1], tankMax[2], fluid.fieldSmoothing},
        {UIntAsFloat(fluid.fluidLattice[0]), UIntAsFloat(fluid.fluidLattice[1]),
         UIntAsFloat(fluid.fluidLattice[2]), UIntAsFloat(fluid.boundaryCount)},
        {UIntAsFloat(wallX), UIntAsFloat(wallY), UIntAsFloat(wallZ), 0.0f},
        {fluid.fillOrigin[0], fluid.fillOrigin[1], fluid.fillOrigin[2], 0.0f},
        {fluid.fieldOrigin[0], fluid.fieldOrigin[1], fluid.fieldOrigin[2], fluid.fieldCell},
        {UIntAsFloat(fluid.fieldDims[0]), UIntAsFloat(fluid.fieldDims[1]),
         UIntAsFloat(fluid.fieldDims[2]), fluid.isoLevel},
        {fieldFixedScale, fieldFixedInvScale,
         UIntAsFloat(kFluidMaxActiveVoxels), UIntAsFloat(kFluidMaxVerts)},
        {fluid.obstacleMin[0], fluid.obstacleMin[1], fluid.obstacleMin[2],
         UIntAsFloat(fluid.obstacleBoundaryCount)},
        {fluid.obstacleMax[0], fluid.obstacleMax[1], fluid.obstacleMax[2], 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f},
    };
    bgfx::setUniform(m_uParams, params, 16);
}

void BgfxFluidRenderer::BindGrid(const FluidGpuState& state) const
{
    bgfx::setBuffer(4, state.cellOf, bgfx::Access::ReadWrite);
    bgfx::setBuffer(5, state.sorted, bgfx::Access::ReadWrite);
    bgfx::setBuffer(6, state.cellStart, bgfx::Access::ReadWrite);
    bgfx::setBuffer(7, state.cellCount, bgfx::Access::ReadWrite);
}

void BgfxFluidRenderer::BindField(const FluidGpuState& state) const
{
    bgfx::setBuffer(8, state.counters, bgfx::Access::ReadWrite);
    bgfx::setBuffer(9, state.fieldAccum, bgfx::Access::ReadWrite);
    bgfx::setBuffer(10, state.voxels, bgfx::Access::ReadWrite);
    bgfx::setBuffer(11, state.voxelOffsets, bgfx::Access::ReadWrite);
    bgfx::setBuffer(12, state.meshVerts, bgfx::Access::ReadWrite);
    bgfx::setBuffer(13, m_triTable, bgfx::Access::Read);
}

void BgfxFluidRenderer::Dispatch(std::uint32_t view, bgfx::ProgramHandle program,
                                 std::uint32_t threads) const
{
    bgfx::dispatch(static_cast<RenderViewHandle>(view), program,
                   DispatchGroups(threads, 64u), 1, 1);
}

void BgfxFluidRenderer::RunSimulation(RenderViewHandle computeView,
                                      const RenderFluid& fluid,
                                      FluidGpuState& state,
                                      float subDt,
                                      float maxSpeed,
                                      bool reset)
{
    const std::uint32_t totalPoints = fluid.particleCount + fluid.boundaryCount;
    const std::uint32_t flags = (reset ? 1u : 0u) | 2u
        | (state.firstField ? 4u : 0u);
    BindParams(fluid, subDt, flags, maxSpeed);

    bgfx::setBuffer(0, state.pos, bgfx::Access::ReadWrite);
    bgfx::setBuffer(1, state.vel, bgfx::Access::ReadWrite);
    bgfx::setBuffer(2, state.prev, bgfx::Access::ReadWrite);
    bgfx::setBuffer(3, state.factors, bgfx::Access::ReadWrite);
    BindGrid(state);
    BindField(state);

    Dispatch(computeView, m_programs[kSlotGridClear],
             std::max(state.numCells, FieldVertexCount(fluid)));
    Dispatch(computeView, m_programs[kSlotIntegrate], totalPoints);
    Dispatch(computeView, m_programs[kSlotGridCount], totalPoints);
    bgfx::dispatch(computeView, m_programs[kSlotGridScan], 1, 1, 1);
    Dispatch(computeView, m_programs[kSlotGridScatter], totalPoints);

    if (subDt <= 0.0f || fluid.particleCount == 0) {
        return;
    }

    Dispatch(computeView, m_programs[kSlotDensity], fluid.particleCount);
    Dispatch(computeView, m_programs[kSlotForces], fluid.particleCount);
    for (std::uint32_t i = 0; i < fluid.divergenceIterations; ++i) {
        Dispatch(computeView, m_programs[kSlotDivergence], fluid.particleCount);
        Dispatch(computeView, m_programs[kSlotApply], fluid.particleCount);
    }
    Dispatch(computeView, m_programs[kSlotIntegrate], fluid.particleCount);
    Dispatch(computeView, m_programs[kSlotGridCount], totalPoints);
    bgfx::dispatch(computeView, m_programs[kSlotGridScan], 1, 1, 1);
    Dispatch(computeView, m_programs[kSlotGridScatter], totalPoints);
    for (std::uint32_t i = 0; i < fluid.densityIterations; ++i) {
        Dispatch(computeView, m_programs[kSlotDensity], fluid.particleCount);
        Dispatch(computeView, m_programs[kSlotPressure], fluid.particleCount);
        Dispatch(computeView, m_programs[kSlotApply], fluid.particleCount);
        Dispatch(computeView, m_programs[kSlotIntegrate], fluid.particleCount);
        Dispatch(computeView, m_programs[kSlotGridCount], totalPoints);
        bgfx::dispatch(computeView, m_programs[kSlotGridScan], 1, 1, 1);
        Dispatch(computeView, m_programs[kSlotGridScatter], totalPoints);
    }
    Dispatch(computeView, m_programs[kSlotDensity], fluid.particleCount);
    Dispatch(computeView, m_programs[kSlotFinalize], fluid.particleCount);
}

void BgfxFluidRenderer::RunReconstruction(RenderViewHandle computeView,
                                          const RenderFluid& fluid,
                                          FluidGpuState& state,
                                          std::uint32_t flags)
{
    const int stage = ReconstructionStage();
    if (stage <= 0 || fluid.particleCount == 0) {
        return;
    }
    const std::uint32_t fieldVerts = FieldVertexCount(fluid);
    const std::uint32_t fieldCells = fluid.fieldDims[0] * fluid.fieldDims[1]
        * fluid.fieldDims[2];

    BindParams(fluid, 0.0f, flags, 0.0f);
    bgfx::setBuffer(0, state.pos, bgfx::Access::Read);
    BindGrid(state);
    BindField(state);

    // Fresh splat target and MC counters. The neighbor-grid arrays are dead
    // for the rest of the frame once the solver is done, so reusing
    // grid_clear here cannot corrupt the simulation.
    Dispatch(computeView, m_programs[kSlotGridClear],
             std::max(state.numCells, fieldVerts));

    // Stage 1: normalized SPH kernel density splat (not a metaball sum — the
    // same kernel the solver measures, so the iso surface conserves volume).
    Dispatch(computeView, m_programs[kSlotFieldSplat], fluid.particleCount);
    if (stage < 2) {
        return;
    }

    // Stage 2: normalization + exponential temporal blend into the other
    // ping-pong 3D texture, which then becomes the field everyone reads.
    {
        const bgfx::TextureHandle readTex = state.fieldTex[state.fieldRead];
        const bgfx::TextureHandle writeTex = state.fieldTex[state.fieldRead ^ 1u];
        bgfx::setTexture(0, m_sFieldTex, readTex);
        bgfx::setImage(1, writeTex, 0, bgfx::Access::Write,
                       bgfx::TextureFormat::R32F);
        bgfx::dispatch(computeView, m_programs[kSlotFieldSmooth],
                       DispatchGroups(fluid.fieldDims[0] + 1u, 4u),
                       DispatchGroups(fluid.fieldDims[1] + 1u, 4u),
                       DispatchGroups(fluid.fieldDims[2] + 1u, 4u));
        state.fieldRead ^= 1u;
    }
    if (stage < 3) {
        return;
    }

    // Stage 3: sparse classify — only iso-straddling voxels reserve geometry.
    bgfx::setTexture(0, m_sFieldTex, state.fieldTex[state.fieldRead]);
    Dispatch(computeView, m_programs[kSlotMcVoxels], fieldCells);
    if (stage < 4) {
        return;
    }

    // Stage 4: canonical-table emission. Dispatch covers the voxel cap
    // because the live count only exists on the GPU; surplus threads exit.
    bgfx::setTexture(0, m_sFieldTex, state.fieldTex[state.fieldRead]);
    bgfx::setImage(2, state.counterTex, 0, bgfx::Access::Write,
                   bgfx::TextureFormat::R32U);
    Dispatch(computeView, m_programs[kSlotMcTriangles], kFluidMaxActiveVoxels);
}

void BgfxFluidRenderer::RunBody(RenderViewHandle computeView,
                                const RenderFluid& fluid,
                                FluidGpuState& state)
{
    const std::uint32_t totalPoints = fluid.particleCount + fluid.boundaryCount;
    if (totalPoints == 0) {
        return;
    }

    const bool reset = fluid.reset || !state.seeded;
    // A reset re-seeds the lattice, so the stale smoothed field from the old
    // particle layout must be discarded rather than blended into.
    if (reset) {
        state.firstField = true;
    }
    const std::uint32_t flags = (reset ? 1u : 0u) | 2u
        | (state.firstField ? 4u : 0u);
    const float frameDt = (!fluid.paused && fluid.deltaTime > 0.0f) ? fluid.deltaTime : 0.0f;
    const std::uint32_t substeps = std::max<std::uint32_t>(fluid.substeps, 1u);
    const float subDt = frameDt > 0.0f ? frameDt / static_cast<float>(substeps) : 0.0f;
    const float maxSpeed = subDt > 1.0e-6f
        ? std::max(0.5f * fluid.kernelRadius / subDt, fluid.spacing / subDt)
        : 0.0f;

    for (std::uint32_t step = 0; step < substeps; ++step) {
        RunSimulation(computeView, fluid, state, subDt, maxSpeed,
                      reset && step == 0);
    }

    RunReconstruction(computeView, fluid, state, flags);
    state.seeded = true;
    state.firstField = false;
}

void BgfxFluidRenderer::Simulate(RenderViewHandle ownerView,
                                 RenderViewHandle computeView,
                                 const RenderFluid* fluids,
                                 std::uint32_t fluidCount)
{
    if (ownerView == kInvalidRenderView || computeView == kInvalidRenderView
        || fluids == nullptr || fluidCount == 0) {
        ++m_frameNumber;
        return;
    }
    if (!EnsureReady()) {
        ++m_frameNumber;
        return;
    }
    for (std::uint32_t i = 0; i < fluidCount; ++i) {
        const RenderFluid& fluid = fluids[i];
        if (fluid.fluidKey == 0 || fluid.particleCount == 0) {
            continue;
        }
        const FluidKey key{ownerView, fluid.fluidKey};
        FluidGpuState& state = EnsureState(key, fluid);
        if (!state.valid) {
            continue;
        }
        RunBody(computeView, fluid, state);
    }
    ++m_frameNumber;
}

} // namespace Concord
