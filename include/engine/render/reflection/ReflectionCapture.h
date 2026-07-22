#ifndef CONCORD_REFLECTIONCAPTURE_H
#define CONCORD_REFLECTIONCAPTURE_H

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>

namespace Concord {

/**
 * Per-view real-time cubemap capture used by reflective mesh materials.
 *
 * The backend renders the current opaque scene into all six faces before the
 * main scene view. Targets are fixed-size and created lazily only when a frame
 * contains a real-time reflective draw.
 */
class ReflectionCapture {
public:
    /** Number of cubemap faces and bgfx views required by one capture. */
    static constexpr std::uint32_t kFaceCount = 6;

    /** Face resolution keeps reflected geometry crisp without unsafe cubemap MSAA resolves. */
    static constexpr std::uint32_t kResolution = 1024;

    /** Cubemap color/depth resources and one framebuffer per face. */
    struct Targets {
        bgfx::TextureHandle color = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle depth = BGFX_INVALID_HANDLE;
        std::array<bgfx::FrameBufferHandle, kFaceCount> framebuffers{{
            BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
            BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};

        /** True when every face can render and the color cubemap can be sampled. */
        bool Valid() const noexcept;
    };

    /** Creates the fixed-size cubemap and face framebuffers when needed. */
    bool EnsureTargets(Targets& targets) const;

    /** Releases all resources and resets @p targets; safe on partial creation. */
    void DestroyTargets(Targets& targets) const;

    /**
     * Builds the standard cubemap view and 90-degree projection for one face.
     * @return false when @p face is outside `[0, kFaceCount)`.
     */
    static bool BuildFaceCamera(std::uint32_t face, const float position[3],
                                bool homogeneousDepth, float outView[16], float outProj[16]);
};

} // namespace Concord

#endif // CONCORD_REFLECTIONCAPTURE_H
