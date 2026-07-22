#ifndef CONCORD_GPULIGHTCULLER_H
#define CONCORD_GPULIGHTCULLER_H

#include "engine/render/backend/IRenderBackend.h"
#include "engine/render/lighting/ClusterGrid.h"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace Concord {

/**
 * Dispatches Forward+ cluster assignment on the render thread.
 *
 * Packed lights and destination textures are supplied per camera so multiple
 * windows and reflection views cannot overwrite each other's cluster lists.
 * Unsupported compute devices and resource-creation failures are reported to
 * the backend, which performs the same assignment with ClusteredLightCuller.
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

    /**
     * Dispatches into the supplied range and index images. The images must
     * belong to the same camera context as lightDataTex and remain alive until
     * that camera's mesh submissions finish.
     */
    void Cull(RenderViewHandle view, bgfx::TextureHandle lightDataTex,
              bgfx::TextureHandle rangeTex, bgfx::TextureHandle indexTex,
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
    bgfx::UniformHandle m_uCullView = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uCullViewProj = BGFX_INVALID_HANDLE;
};

} // namespace Concord

#endif // CONCORD_GPULIGHTCULLER_H
