#ifndef CONCORD_BGFXSMAA_H
#define CONCORD_BGFXSMAA_H

#include "engine/render/backend/IRenderBackend.h"
#include "engine/render/postprocess/AntiAliasing.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <unordered_map>

namespace Concord {

/**
 * Subpixel morphological anti-aliasing (SMAA) as a three-pass pipeline: edge
 * detection, blend-weight calculation, then neighborhood blending.
 *
 * Given the scene already rendered into a color texture, Run executes the three
 * passes into its own intermediate render targets and returns the anti-aliased
 * result texture; the caller then blits that to the window (which is where the
 * backend's render-target-vs-window vertical flip is applied). Keeping every
 * SMAA pass flip-free and in the same texture space is what makes the multi-pass
 * chain line up correctly.
 *
 * The blend-weight pass follows the reference SMAA 1x implementation, including
 * orthogonal and diagonal searches, corner detection, and the original RG8
 * AreaTex/R8 SearchTex lookup tables generated in C++. SMAA2 uses the reference
 * High preset; SMAA4 uses Ultra. Intermediate targets are cached per view and
 * rebuilt on resize. Render-thread only, like the backend that owns it.
 */
class BgfxSmaa {
public:
    /**
     * Lazily creates the three programs, samplers, params uniform and the
     * shared fullscreen-triangle vertex buffer.
     * @return true once ready; false if creation failed.
     */
    bool EnsureReady();

    /** Destroys all programs, uniforms, the vertex buffer and every view's targets. */
    void Shutdown();

    /**
     * Runs the three SMAA passes for `key`'s view over `sceneColor`, using the
     * three supplied bgfx view ids (which must order after the scene view).
     * @return the anti-aliased color texture, or an invalid handle on failure.
     */
    bgfx::TextureHandle Run(RenderViewHandle key,
                            RenderViewHandle edgeView, RenderViewHandle weightView, RenderViewHandle blendView,
                            bgfx::TextureHandle sceneColor, std::uint32_t width, std::uint32_t height,
                            AntiAliasing mode);

    /** Frees the cached intermediate targets for a destroyed view. */
    void Release(RenderViewHandle key);

private:
    /** One view's intermediate targets: edges, blend weights, and the final AA result. */
    struct Targets {
        bgfx::FrameBufferHandle edgesFb = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle edges = BGFX_INVALID_HANDLE;
        bgfx::FrameBufferHandle weightsFb = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle weights = BGFX_INVALID_HANDLE;
        bgfx::FrameBufferHandle resultFb = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle result = BGFX_INVALID_HANDLE;
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        bool Matches(std::uint32_t w, std::uint32_t h) const noexcept
        {
            return bgfx::isValid(resultFb) && width == w && height == h;
        }
    };

    Targets* EnsureTargets(RenderViewHandle key, std::uint32_t width, std::uint32_t height);
    void DestroyTargets(Targets& targets);
    void DestroyResources();

    bool m_ready = false;
    bool m_attempted = false;
    bgfx::ProgramHandle m_edgesProgram = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_weightsProgram = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_blendProgram = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sEdges = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sWeights = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sArea = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sSearch = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uTexel = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uConfig = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uPresentParams = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_areaTexture = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_searchTexture = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_fullscreenVb = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout m_layout;
    std::unordered_map<RenderViewHandle, Targets> m_targets;
};

} // namespace Concord

#endif // CONCORD_BGFXSMAA_H
