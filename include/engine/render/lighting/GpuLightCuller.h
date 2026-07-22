#ifndef CONCORD_GPULIGHTCULLER_H
#define CONCORD_GPULIGHTCULLER_H

#include "engine/render/backend/IRenderBackend.h"
#include "engine/render/lighting/ClusterGrid.h"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace Concord {

/**
 * GPU compute Forward+ light culler (Phase 3): dispatches one thread per
 * cluster to build the same per-cluster (offset,count) range texture and flat
 * light-index texture the CPU `ClusteredLightCuller` produces, so `fs_mesh`
 * consumes either backend's output identically (see design.md "Components and
 * Interfaces"). The light-data texture itself is still packed and uploaded on
 * the CPU (cheap, O(lights)); only the O(lights x clusters) assignment work
 * moves to the GPU.
 *
 * Falls back cleanly: `Supported()` reports whether the active backend
 * advertises compute; when false (or `EnsureReady` fails) the backend must keep
 * using the CPU `ClusteredLightCuller` (Requirement 5.4). All methods run on
 * the render thread.
 */
class GpuLightCuller {
public:
    ~GpuLightCuller();

    GpuLightCuller(const GpuLightCuller&) = delete;
    GpuLightCuller& operator=(const GpuLightCuller&) = delete;
    GpuLightCuller() = default;

    /** True when the active renderer advertises compute-shader support. */
    bool Supported() const;

    /** Lazily creates the compute program + uniforms + output images. Idempotent. */
    bool EnsureReady();

    /** Releases every resource; safe when never readied or already shut down. */
    void Shutdown();

    /** Whether EnsureReady has succeeded and Cull can run. */
    bool Ready() const noexcept { return m_ready; }

    /** Output range texture (RGBA32F, one texel per cluster: offset,count). */
    bgfx::TextureHandle RangeTexture() const noexcept { return m_rangeTex; }

    /** Output flat light-index texture (R32F, 1024 wide). */
    bgfx::TextureHandle IndexTexture() const noexcept { return m_indexTex; }

    /**
     * Dispatches the cull compute on `view` (a dedicated bgfx view id, lower
     * than the scene view so results are ready when it samples them).
     * `lightDataTex` is the CPU-packed light data texture (same layout
     * GpuLight::Pack produces); `lightCount`/`directionalCount` describe it.
     * A no-op when not Ready.
     */
    void Cull(RenderViewHandle view, bgfx::TextureHandle lightDataTex,
              std::uint32_t lightCount, std::uint32_t directionalCount,
              const float viewMatrix[16], const float viewProj[16],
              const ClusterGrid& grid);

private:
    void DestroyResources();

    bool m_ready = false;
    bool m_attempted = false;
    bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sLightData = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uCullParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uCullScreen = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uCullView = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uCullViewProj = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_rangeTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_indexTex = BGFX_INVALID_HANDLE;
};

} // namespace Concord

#endif // CONCORD_GPULIGHTCULLER_H
