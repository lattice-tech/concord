#ifndef CONCORD_BGFXBLOOM_H
#define CONCORD_BGFXBLOOM_H

#include "engine/render/backend/IRenderBackend.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <vector>

namespace Concord {

/**
 * HDR bloom via the Call of Duty: Advanced Warfare / Jimenez mip-chain method
 * (the modern-engine standard).
 *
 * The bright HDR energy of the offscreen scene is downsampled through a chain
 * of progressively smaller RGBA16F mips with a 13-tap filter (stable, no
 * shimmer), then upsampled back up with a 3x3 tent filter that is additively
 * accumulated so each level gathers the blur of every smaller level. The
 * result is a wide, soft, artefact-free glow at a fraction of a naive
 * large-kernel blur's cost (the whole chain is ~1.3x the half-res pixel count).
 *
 * All passes are no-flip fullscreen passes between offscreen targets, matching
 * the SMAA post-process orientation convention; the present pass composites
 * mip 0 with the same UV as the scene, so no mirrored ghost appears. Runs on
 * the render thread and only touches bgfx.
 */
class BgfxBloom {
public:
    /** Maximum mip levels in the chain (each half the previous resolution). */
    static constexpr std::uint32_t kMaxMips = 5;

    /** View ids a caller must provide to Generate: one per down + up pass. */
    static constexpr std::uint32_t kMaxViews = 2 * kMaxMips;

    /** One mip level of the chain: a half-of-previous RGBA16F color target. */
    struct Mip {
        bgfx::TextureHandle tex = BGFX_INVALID_HANDLE;
        bgfx::FrameBufferHandle fb = BGFX_INVALID_HANDLE;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    /** One view's bloom mip chain (starts at half the window resolution). */
    struct Targets {
        std::vector<Mip> mips;
        Mip result;

        /** True once the chain has at least one usable mip. */
        bool Valid() const noexcept
        {
            return !mips.empty() && bgfx::isValid(mips.front().fb)
                && bgfx::isValid(result.fb);
        }
    };

    /** Lazily creates the down/up programs, uniforms and fullscreen buffer. */
    bool EnsureReady();

    /** Destroys the shared bloom resources. Safe when never readied. */
    void Shutdown();

    /**
     * Builds the mip chain for a `fullWidth` x `fullHeight` window: a series of
     * half-then-quarter-etc RGBA16F targets down to kMaxMips levels (or until a
     * dimension would reach 1). @return true on success.
     */
    bool CreateTargets(std::uint32_t fullWidth, std::uint32_t fullHeight, Targets& out);

    /** Destroys the mip chain, resetting `targets` to empty. */
    void DestroyTargets(Targets& targets);

    /**
     * Runs the downsample + additive upsample chain and a final tent blur into
     * a separate result target. `views` must point to at
     * least kMaxViews render-view ids (ordered, all after the scene view and
     * before present). `sceneColor` is the full-res HDR scene; `threshold` is
     * the first-pass bright cutoff; `filterRadius` is the tent-filter radius
     * in source texels. @return the finished bloom texture (mip 0), or invalid
     * if not ready.
     */
    bgfx::TextureHandle Generate(const RenderViewHandle* views, std::uint32_t viewCount,
                                 bgfx::TextureHandle sceneColor,
                                 std::uint32_t fullWidth, std::uint32_t fullHeight,
                                  const Targets& targets, float threshold, float filterRadius);

private:
    void DestroyResources();
    void DrawPass(RenderViewHandle view, bgfx::FrameBufferHandle target,
                  bgfx::TextureHandle source, bgfx::ProgramHandle program,
                  const float params[4], std::uint32_t width, std::uint32_t height, bool additive);

    bool m_ready = false;
    bool m_attempted = false;
    bgfx::ProgramHandle m_downProgram = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_upProgram = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sTex = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uParams = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_fullscreenVb = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout m_layout;
};

} // namespace Concord

#endif // CONCORD_BGFXBLOOM_H
