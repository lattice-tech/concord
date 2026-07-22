#ifndef CONCORD_SHADOWMAP_H
#define CONCORD_SHADOWMAP_H

#include "engine/render/backend/IRenderBackend.h"
#include "engine/render/shadow/ShadowConfig.h"

#include <bgfx/bgfx.h>

#include <array>

namespace Concord {

/**
 * bgfx lifeline for the single directional-light shadow pass.
 *
 * Owns the depth-pass program (`vs_shadow` + `fs_shadow`), the lighting-pass
 * uniforms and the shadow sampler that `fs_mesh` reads, plus the helper that
 * builds a per-view depth-only render target. The render backend holds one
 * instance and drives the per-frame sequence:
 *
 *   `EnsureReady` once per process; for each window `CreateViewTarget`;
 *   every frame, `BeginDepthPass` (configure the shadow view) -> the backend
 *   submits the batches into that view with `Program` -> `BindForSampling`
 *   for the main mesh pass. `BindDisabled` covers a frame or batch with no
 *   shadow caster so the shader's shadow inputs are never left unset.
 *
 * All entries run on the render thread and only touch bgfx, just like the rest
 * of `BgfxRenderBackend` (AGENTS.md §5 — kept in its own unit so the shadow
 * concern is isolated from the scene submission concern).
 */
class ShadowMap {
public:
    ShadowMap() = default;
    ~ShadowMap();

    /**
     * Lazily creates the depth-pass program and the shared lighting-pass
     * uniforms/sampler. Idempotent: a failed attempt is not retried until
     * `Shutdown`. Safe to call before bgfx init.
     * @return true once the program and uniforms are ready for use.
     */
    bool EnsureReady();

    /** Releases the program and uniforms; safe when never ready or already shut down. */
    void Shutdown();

    /**
     * Allocates one per-scene-view depth-only render target at the configured
     * resolution, returning its framebuffer and the sampleable depth texture.
     * @return false if bgfx refused either resource (the caller then has nothing
     *         to bind and `Shutdown`-safe `DestroyViewTarget` skips them).
     */
    bool CreateViewTarget(std::uint32_t resolution,
                          bgfx::FrameBufferHandle& fb,
                          bgfx::TextureHandle& tex) const;

    /** Frees a per-view target previously handed out by `CreateViewTarget`. */
    void DestroyViewTarget(bgfx::FrameBufferHandle& fb, bgfx::TextureHandle& tex) const;

    /**
     * Configures the shadow view: framebuffer, square rect at `config.resolution`,
     * depth-only clear, sequential view mode, and the light view / projection
     * transform that the depth-pass vertex shader picks up as `u_viewProj`.
     */
    void BeginDepthPass(RenderViewHandle shadowView, bgfx::FrameBufferHandle fb,
                        const ShadowConfig& config,
                        const float lightView[16], const float lightProj[16]) const;

    /**
     * Binds the shadow sampler (slot 4) to `depthTex`, uploads the light
     * view-projection matrix and the bias / texel-size / enable parameters for
     * one main-pass submit. Call once per submitted batch — bgfx's transient
     * sampler state is consumed and reset by every `submit`.
     */
    void BindForSampling(
                         const std::array<bgfx::TextureHandle, kShadowCascadeCount>& depthTextures,
                         const std::array<std::array<float, 16>, kShadowCascadeCount>& lightViewProj,
                         const float cameraView[16], const float lightDir[3],
                         const std::array<float, kShadowCascadeCount>& splitDepths,
                         const std::array<float, kShadowCascadeCount>& blendWidths,
                         const std::array<float, kShadowCascadeCount>& penumbraScaleTexels,
                         const std::array<float, kShadowCascadeCount>& normalBiasWorld,
                         int casterIndex,
                         const ShadowConfig& config) const;

    /**
     * Same as `BindForSampling` for a frame or batch with no shadow caster:
     * binds a neutral texture (the engine's 1x1 white) and uploads
     * `u_shadowParams.w = 0` so the shader early-outs and behaves lit. The
     * light-view-projection matrix is whatever was set on the last
     * `BindForSampling` (an identity the first frame), which the shader never
     * reads while the disabled flag is set.
     */
    void BindDisabled(bgfx::TextureHandle whiteTexture) const;

    /** The shadow depth-pass program. Only valid after a successful `EnsureReady`. */
    bgfx::ProgramHandle Program() const noexcept { return m_program; }

    /** Shadow depth program that applies the draw's bone palette before projection. */
    bgfx::ProgramHandle SkinnedProgram() const noexcept { return m_skinnedProgram; }

private:
    bool m_ready = false;
    bool m_attempted = false;
    bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_skinnedProgram = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uLightViewProj = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uShadowCameraView = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uShadowCascadeSplits = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uShadowCascadeBlend = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uShadowPenumbra = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uShadowNormalBias = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uShadowParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uShadowLightDir = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uShadowFilter = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uShadowProjection = BGFX_INVALID_HANDLE;
    std::array<bgfx::UniformHandle, kShadowCascadeCount> m_sShadowMaps{{
        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
};

} // namespace Concord

#endif // CONCORD_SHADOWMAP_H
