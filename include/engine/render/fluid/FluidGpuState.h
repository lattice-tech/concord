#ifndef CONCORD_FLUIDGPUSTATE_H
#define CONCORD_FLUIDGPUSTATE_H

#include "engine/render/frame/RenderFluid.h"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace Concord {

/**
 * @brief Persistent per-body GPU resources of one DFSPH fluid.
 *
 * Every buffer lives in the fluid's local simulation frame. Particles and
 * grid are plain compute buffers (bgfx has no raw storage-buffer type, so —
 * like the GPU particle pool — they are dynamic vertex buffers with compute
 * flags, viewed by the shaders as typed arrays). The scalar field ping-pongs
 * between two R32F 3D textures: the compute smooth pass writes one while
 * sampling the other, the MC passes and the refraction march then sample the
 * freshly written one, and the roles swap next frame.
 */
struct FluidGpuState {
    bgfx::DynamicVertexBufferHandle pos = BGFX_INVALID_HANDLE;      ///< vec4 xyz + density.
    bgfx::DynamicVertexBufferHandle vel = BGFX_INVALID_HANDLE;      ///< vec4 xyz + alpha factor.
    bgfx::DynamicVertexBufferHandle prev = BGFX_INVALID_HANDLE;     ///< vec4 previous position.
    bgfx::DynamicVertexBufferHandle factors = BGFX_INVALID_HANDLE;  ///< float per-particle k.
    bgfx::DynamicVertexBufferHandle cellOf = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle sorted = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle cellStart = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle cellCount = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle counters = BGFX_INVALID_HANDLE; ///< 4 uints.
    bgfx::DynamicVertexBufferHandle fieldAccum = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle voxels = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle voxelOffsets = BGFX_INVALID_HANDLE;
    bgfx::DynamicVertexBufferHandle meshVerts = BGFX_INVALID_HANDLE; ///< drawable MC stream.

    bgfx::TextureHandle fieldTex[2]{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
    bgfx::TextureHandle counterTex = BGFX_INVALID_HANDLE; ///< 1x1 R32U drawn-vertex count.
    std::uint32_t fieldRead = 0;   ///< Index of the latest smoothed field texture.

    std::uint32_t particleCount = 0;
    std::uint32_t boundaryCount = 0;
    std::uint32_t numCells = 0;
    std::uint32_t fieldVertCount = 0;
    std::uint32_t maxVoxels = 0;
    std::uint32_t maxVerts = 0;
    std::uint32_t fieldDims[3]{0, 0, 0};

    std::uint64_t lastSeenFrame = 0;
    std::uint32_t lastDrawnVerts = 0;
    bool seeded = false;
    bool firstField = true;
    bool valid = false;
};

/**
 * Caps for the sparse Marching Cubes work queues. Active voxels are bounded
 * so a splashy frame cannot allocate unbounded memory; the vertex cap keeps
 * the drawable stream at ~7 MB worst case.
 */
inline constexpr std::uint32_t kFluidMaxActiveVoxels = 24576;
inline constexpr std::uint32_t kFluidMaxVerts = kFluidMaxActiveVoxels * 12;

/**
 * Creates every buffer/texture for the resolved body. Existing content is
 * discarded; the caller must re-seed afterwards. Returns false (leaving the
 * state invalid) when any allocation fails.
 */
bool CreateFluidGpuState(FluidGpuState& state, const RenderFluid& fluid);

/** Releases all resources; safe on a default or already-destroyed state. */
void DestroyFluidGpuState(FluidGpuState& state);

/**
 * True when the state's allocation matches the resolved body — a change in
 * particle count, grid or field resolution requires recreation.
 */
bool FluidGpuStateMatches(const FluidGpuState& state, const RenderFluid& fluid);

} // namespace Concord

#endif // CONCORD_FLUIDGPUSTATE_H
