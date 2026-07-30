#include "engine/render/fluid/FluidGpuState.h"

namespace {

bgfx::DynamicVertexBufferHandle CreateComputeBuffer(std::uint32_t elements,
                                                    std::uint32_t strideFloats)
{
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, static_cast<std::uint8_t>(strideFloats),
             bgfx::AttribType::Float)
        .end();
    return bgfx::createDynamicVertexBuffer(elements, layout,
                                           BGFX_BUFFER_COMPUTE_READ_WRITE);
}

bgfx::DynamicVertexBufferHandle CreateMeshBuffer(std::uint32_t vertices)
{
    bgfx::VertexLayout layout;
    layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
        .end();
    return bgfx::createDynamicVertexBuffer(vertices, layout,
                                           BGFX_BUFFER_COMPUTE_READ_WRITE);
}

} // namespace

namespace Concord {

bool CreateFluidGpuState(FluidGpuState& state, const RenderFluid& fluid)
{
    DestroyFluidGpuState(state);
    const std::uint32_t totalPoints = fluid.particleCount + fluid.boundaryCount;
    state.particleCount = fluid.particleCount;
    state.boundaryCount = fluid.boundaryCount;
    state.numCells = fluid.gridDims[0] * fluid.gridDims[1] * fluid.gridDims[2];
    state.fieldDims[0] = fluid.fieldDims[0];
    state.fieldDims[1] = fluid.fieldDims[1];
    state.fieldDims[2] = fluid.fieldDims[2];
    state.fieldVertCount = (fluid.fieldDims[0] + 1) * (fluid.fieldDims[1] + 1)
        * (fluid.fieldDims[2] + 1);
    state.maxVoxels = kFluidMaxActiveVoxels;
    state.maxVerts = kFluidMaxVerts;

    state.pos = CreateComputeBuffer(totalPoints, 4);
    state.vel = CreateComputeBuffer(totalPoints, 4);
    state.prev = CreateComputeBuffer(totalPoints, 4);
    state.factors = CreateComputeBuffer(fluid.particleCount, 1);
    state.cellOf = CreateComputeBuffer(totalPoints, 1);
    state.sorted = CreateComputeBuffer(totalPoints, 1);
    state.cellStart = CreateComputeBuffer(state.numCells, 1);
    state.cellCount = CreateComputeBuffer(state.numCells, 1);
    state.counters = CreateComputeBuffer(4, 1);
    state.fieldAccum = CreateComputeBuffer(state.fieldVertCount, 1);
    state.voxels = CreateComputeBuffer(state.maxVoxels, 1);
    state.voxelOffsets = CreateComputeBuffer(state.maxVoxels, 1);
    state.meshVerts = CreateMeshBuffer(state.maxVerts);

    for (bgfx::TextureHandle& tex : state.fieldTex) {
        tex = bgfx::createTexture3D(
            static_cast<std::uint16_t>(fluid.fieldDims[0] + 1),
            static_cast<std::uint16_t>(fluid.fieldDims[1] + 1),
            static_cast<std::uint16_t>(fluid.fieldDims[2] + 1), false,
            bgfx::TextureFormat::R32F,
            BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_U_CLAMP
                | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP);
    }
    state.counterTex = bgfx::createTexture2D(
        1, 1, false, 1, bgfx::TextureFormat::R32U,
        BGFX_TEXTURE_COMPUTE_WRITE | BGFX_SAMPLER_POINT);

    state.valid = bgfx::isValid(state.pos) && bgfx::isValid(state.vel)
        && bgfx::isValid(state.prev) && bgfx::isValid(state.factors)
        && bgfx::isValid(state.cellOf) && bgfx::isValid(state.sorted)
        && bgfx::isValid(state.cellStart) && bgfx::isValid(state.cellCount)
        && bgfx::isValid(state.counters) && bgfx::isValid(state.fieldAccum)
        && bgfx::isValid(state.voxels) && bgfx::isValid(state.voxelOffsets)
        && bgfx::isValid(state.meshVerts)
        && bgfx::isValid(state.fieldTex[0]) && bgfx::isValid(state.fieldTex[1])
        && bgfx::isValid(state.counterTex);
    state.seeded = false;
    state.firstField = true;
    state.fieldRead = 0;
    state.lastDrawnVerts = 0;
    if (!state.valid) {
        DestroyFluidGpuState(state);
    }
    return state.valid;
}

void DestroyFluidGpuState(FluidGpuState& state)
{
    bgfx::DynamicVertexBufferHandle buffers[] = {
        state.pos, state.vel, state.prev, state.factors, state.cellOf,
        state.sorted, state.cellStart, state.cellCount, state.counters,
        state.fieldAccum, state.voxels, state.voxelOffsets, state.meshVerts,
    };
    for (bgfx::DynamicVertexBufferHandle& buffer : buffers) {
        if (bgfx::isValid(buffer)) {
            bgfx::destroy(buffer);
        }
        buffer = BGFX_INVALID_HANDLE;
    }
    for (bgfx::TextureHandle& tex : state.fieldTex) {
        if (bgfx::isValid(tex)) {
            bgfx::destroy(tex);
        }
        tex = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(state.counterTex)) {
        bgfx::destroy(state.counterTex);
    }
    state.counterTex = BGFX_INVALID_HANDLE;
    state.valid = false;
    state.seeded = false;
    state.lastDrawnVerts = 0;
}

bool FluidGpuStateMatches(const FluidGpuState& state, const RenderFluid& fluid)
{
    return state.valid && state.particleCount == fluid.particleCount
        && state.boundaryCount == fluid.boundaryCount
        && state.numCells == fluid.gridDims[0] * fluid.gridDims[1] * fluid.gridDims[2]
        && state.fieldDims[0] == fluid.fieldDims[0]
        && state.fieldDims[1] == fluid.fieldDims[1]
        && state.fieldDims[2] == fluid.fieldDims[2];
}

} // namespace Concord
