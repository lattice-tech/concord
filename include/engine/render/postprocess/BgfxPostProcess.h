#ifndef CONCORD_BGFXPOSTPROCESS_H
#define CONCORD_BGFXPOSTPROCESS_H

#include "engine/render/postprocess/AntiAliasing.h"
#include "engine/render/backend/IRenderBackend.h"
#include "engine/render/frame/ViewEffectState.h"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace Concord {

/**
 * The offscreen-render-target and fullscreen present machinery behind
 * post-process anti-aliasing (Requirement 4).
 *
 * When a view uses a post-process AA mode, the backend renders the scene into
 * one of these offscreen Targets (a color texture plus a depth texture) instead
 * of straight to the window, then asks this pipeline to run a fullscreen pass
 * that samples the resolved color and writes it to the window's framebuffer —
 * applying FXAA (or an SMAA preset) on the way. Kept in its own unit so the
 * render backend stays focused on scene submission and the screen-space AA
 * concern lives on its own (AGENTS.md §5).
 *
 * All methods run on the render thread and only touch bgfx, so — like the
 * backend that owns it — it is single-threaded by construction. The owning
 * backend creates it after bgfx init and calls Shutdown before bgfx::shutdown.
 */
class BgfxPostProcess {
public:
    /** One view's offscreen scene target: a sampleable color plus a depth buffer. */
    struct Targets {
        bgfx::FrameBufferHandle framebuffer = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle color = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle depth = BGFX_INVALID_HANDLE;

        /** True once CreateTargets has populated a usable framebuffer. */
        bool Valid() const noexcept { return bgfx::isValid(framebuffer); }
    };

    /**
     * Lazily creates the shared present resources (program, sampler, params
     * uniform, fullscreen-triangle vertex buffer) on first use.
     * @return true once they are ready; false if creation failed (the caller
     *         then falls back to direct-to-window rendering).
     */
    bool EnsureReady();

    /** Destroys the shared present resources. Safe to call when never readied. */
    void Shutdown();

    /**
     * Creates an offscreen color+depth target sized `width` x `height`.
     * Any resources already owned by `out` are released first.
     * @return true on success; on failure `out` is left empty/invalid.
     */
    bool CreateTargets(std::uint32_t width, std::uint32_t height, Targets& out);

    /** Destroys the textures and framebuffer of `targets`, resetting it to empty. */
    void DestroyTargets(Targets& targets);

    /**
     * Runs the fullscreen AA pass: samples `color` and writes it to whatever
     * framebuffer is bound to bgfx view `presentView`, applying the technique
     * `mode` selects (FXAA, or an SMAA quality preset). A no-op if the pipeline
     * is not ready or `color` is invalid.
     */
    void Present(RenderViewHandle presentView, bgfx::TextureHandle color,
                 std::uint32_t width, std::uint32_t height, AntiAliasing mode,
                 bgfx::TextureHandle bloom = BGFX_INVALID_HANDLE, float bloomIntensity = 0.0f,
                 const ViewEffectState* effects = nullptr);

    /**
     * Blits an already-anti-aliased `color` straight to view `presentView`'s
     * framebuffer (a passthrough copy with the render-target-vs-window V flip),
     * used to present the SMAA result. A no-op if the pipeline is not ready.
     * Optionally composites `bloom` on top with `bloomIntensity`.
     */
    void Blit(RenderViewHandle presentView, bgfx::TextureHandle color,
              std::uint32_t width, std::uint32_t height,
              bgfx::TextureHandle bloom = BGFX_INVALID_HANDLE, float bloomIntensity = 0.0f,
              const ViewEffectState* effects = nullptr);

private:
    void DestroyResources();
    void Draw(RenderViewHandle presentView, bgfx::TextureHandle color,
              std::uint32_t width, std::uint32_t height, float quality,
              bgfx::TextureHandle bloom, float bloomIntensity,
              const ViewEffectState* effects);

    bool m_ready = false;
    bool m_attempted = false;
    bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sScene = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sBloom = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uBloomComposite = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uScreenShake = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uMagnifier = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uMagnifierParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uLensFlare = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_fullscreenVb = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout m_layout;
};

} // namespace Concord

#endif // CONCORD_BGFXPOSTPROCESS_H
