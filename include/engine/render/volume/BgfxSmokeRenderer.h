#ifndef CONCORD_BGFXSMOKERENDERER_H
#define CONCORD_BGFXSMOKERENDERER_H

#include "engine/render/backend/IRenderBackend.h"
#include "engine/render/frame/RenderLight.h"
#include "engine/render/frame/RenderSmokeVolume.h"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace Concord {

/**
 * Local volumetric smoke pass: composites author-placed SmokeVolume boxes into
 * the HDR scene color.
 *
 * Unlike the sky's global volumetric clouds, smoke volumes are bounded local
 * media placed as scene nodes. This first version runs a single full-resolution
 * fullscreen pass straight into the offscreen scene target: for each pixel it
 * reconstructs the world ray, reads the full-resolution scene depth to truncate
 * at the nearest opaque surface, then for every active volume computes the
 * analytic Beer-Lambert absorption over the segment the ray spends inside the
 * axis-aligned box (uniform density). The half-resolution march + depth-aware
 * upsample optimization and FBM density / single scattering are deliberately
 * deferred to later phases.
 *
 * This renderer owns only its program, uniforms and fullscreen triangle; the
 * scene framebuffer/depth and the smoke view id are owned per-window by the
 * backend (ViewSlot) and passed in. It allocates no bgfx views of its own. All
 * methods run on the render thread.
 */
class BgfxSmokeRenderer {
public:
    /** Lazily creates the march program, uniforms and fullscreen triangle. */
    bool EnsureReady();

    /** Releases every GPU resource; safe before initialization and after shutdown. */
    void Shutdown();

    /**
     * Fraction of full resolution the smoke march runs at (1/N width and
     * height). Currently 1 (full resolution) to keep the sharpest image; a
     * later depth-aware (bilateral) upsample will let this drop to 2 for the
     * performance win without the blur/halo a naive half-res upsample causes.
     * The reflection cubemap composite always marches at face resolution.
     */
    static constexpr std::uint32_t kResolutionDivisor = 2;

    /** Inputs for one smoke composite; targets/handles are owned by the caller. */
    struct DrawParams {
        RenderViewHandle marchView = kInvalidRenderView;     ///< Low-res march view (main) or face view (compose-only).
        RenderViewHandle compositeView = kInvalidRenderView; ///< Full-res upsample view (main path only).
        bgfx::FrameBufferHandle sceneFb = BGFX_INVALID_HANDLE; ///< Full-res HDR scene target (composite target).
        bgfx::TextureHandle sceneDepth = BGFX_INVALID_HANDLE;  ///< Full-res scene depth.
        bgfx::FrameBufferHandle lowResFb = BGFX_INVALID_HANDLE; ///< Low-res march target (main path).
        bgfx::TextureHandle lowResColor = BGFX_INVALID_HANDLE;  ///< Low-res march result, sampled by composite.
        bgfx::TextureHandle lowResDepth = BGFX_INVALID_HANDLE;  ///< Low-res device-depth proxy (MRT 2nd attachment).
        std::uint32_t lowWidth = 0;
        std::uint32_t lowHeight = 0;
        std::uint32_t fullWidth = 0;
        std::uint32_t fullHeight = 0;
        const float* viewMatrix = nullptr;
        const float* projectionMatrix = nullptr;
        const float* eye = nullptr;
        const RenderLight* lights = nullptr;
        std::uint32_t lightCount = 0;
        const RenderSmokeVolume* volumes = nullptr;
        std::uint32_t volumeCount = 0;

        /**
         * Number of march steps; 0 uses the default. The reflection cubemap pass
         * passes a smaller count to keep the amortized per-face march cheap.
         */
        float steps = 0.0f;

        /**
         * When true the caller has already configured `marchView` (framebuffer,
         * rect, clear, mode) and will submit sequentially, so this pass runs a
         * single full-resolution march directly onto that view — used to
         * composite onto an in-progress reflection cubemap face. `sceneDepth` is
         * then expected to be a 1.0 (white) texture (the cubemap's depth is
         * write-only, not sampleable) so the march is not depth-truncated.
         */
        bool composeOnly = false;
    };

    /** Composites every active volume over the scene color with premultiplied alpha. */
    void Draw(const DrawParams& params);

private:
    void DestroyResources();

    bool m_ready = false;
    bool m_attempted = false;
    bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE;       ///< Main path: MRT (color + depth proxy).
    bgfx::ProgramHandle m_programSingle = BGFX_INVALID_HANDLE; ///< Compose-only path: single color attachment.
    bgfx::ProgramHandle m_compositeProgram = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sComposite = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sLowDepth = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sFullDepth = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uUpsample = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uInvViewProj = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uCamera = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uSunDir = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uBoxMin = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uBoxMax = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uShape = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uWind = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sSceneDepth = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sNoise = BGFX_INVALID_HANDLE;
    /** Precomputed tileable 3D FBM noise (R broad shape, G fine detail). */
    bgfx::TextureHandle m_noiseTexture = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_fullscreenVb = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout m_layout;
};

} // namespace Concord

#endif // CONCORD_BGFXSMOKERENDERER_H
