#ifndef CONCORD_BGFXVOLUMECLOUDRENDERER_H
#define CONCORD_BGFXVOLUMECLOUDRENDERER_H

#include "engine/render/backend/IRenderBackend.h"
#include "engine/render/frame/RenderLight.h"
#include "engine/render/frame/SkyEnvironment.h"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace Concord {

/**
 * Independent, half-resolution volumetric cloud pass.
 *
 * Follows the approach real-time engines (e.g. UE's Volumetric Clouds) use for
 * performance: the expensive ray march runs into a half-resolution offscreen
 * target, then a cheap fullscreen pass upsamples that result and composites it
 * over the full-resolution scene color with premultiplied alpha, before bloom
 * and tone mapping. Marching a quarter of the pixels is the dominant cost saving
 * versus a full-resolution march.
 *
 * The march samples the full-resolution scene depth to truncate at the nearest
 * opaque surface, so scene geometry correctly occludes the clouds. The
 * low-resolution target and its framebuffer are owned per-window by the backend
 * (ViewSlot) and passed in; this renderer only owns the shared programs,
 * uniforms and fullscreen triangle. It allocates no bgfx views of its own; the
 * backend hands it two view ids (march + composite) ordered after the scene
 * view and before bloom/present. All methods run on the render thread.
 */
class BgfxVolumeCloudRenderer {
public:
    /**
     * Fraction of full resolution the cloud march runs at (1/N width and
     * height). The clouds are low-frequency FBM faded out at the horizon, so a
     * third-resolution march upsamples cleanly while cutting the march to ~1/9
     * of the pixels. Raise toward 1-2 for sharper clouds at a higher GPU cost.
     */
    static constexpr std::uint32_t kResolutionDivisor = 3;

    /** Lazily creates the march + composite programs, uniforms and fullscreen triangle. */
    bool EnsureReady();

    /** Releases every GPU resource; safe before initialization and after shutdown. */
    void Shutdown();

    /** Whether the authored environment requests a visible cloud layer. */
    static bool Enabled(const SkyEnvironment& environment) noexcept
    {
        return environment.clouds && environment.mode == SkyMode::Procedural;
    }

    /** Inputs for one cloud composite; targets/handles are owned by the caller. */
    struct DrawParams {
        RenderViewHandle marchView = kInvalidRenderView;     ///< Low-res march view id.
        RenderViewHandle compositeView = kInvalidRenderView; ///< Full-res upsample view id.
        bgfx::FrameBufferHandle lowResFb = BGFX_INVALID_HANDLE; ///< Wraps lowResColor.
        bgfx::TextureHandle lowResColor = BGFX_INVALID_HANDLE;  ///< Half-res march result.
        std::uint32_t lowWidth = 0;
        std::uint32_t lowHeight = 0;
        bgfx::FrameBufferHandle sceneFb = BGFX_INVALID_HANDLE;  ///< Full-res scene target.
        bgfx::TextureHandle sceneDepth = BGFX_INVALID_HANDLE;   ///< Full-res scene depth.
        std::uint32_t fullWidth = 0;
        std::uint32_t fullHeight = 0;
        const float* viewMatrix = nullptr;
        const float* projectionMatrix = nullptr;
        const float* eye = nullptr;
        const RenderLight* lights = nullptr;
        std::uint32_t lightCount = 0;
    };

    /** Marches clouds at half resolution then upsamples/composites into the scene. */
    void Draw(const DrawParams& params, const SkyEnvironment& environment);

private:
    void DestroyResources();

    bool m_ready = false;
    bool m_attempted = false;
    bgfx::ProgramHandle m_marchProgram = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_compositeProgram = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uInvViewProj = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uCamera = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uLayer = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uShape = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uMotion = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uLit = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uShadow = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uFire = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uSunDir = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sSceneDepth = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sCloud = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_fullscreenVb = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout m_layout;
};

} // namespace Concord

#endif // CONCORD_BGFXVOLUMECLOUDRENDERER_H
