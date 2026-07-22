#ifndef CONCORD_PLANARREFLECTION_H
#define CONCORD_PLANARREFLECTION_H

#include "engine/render/backend/IRenderBackend.h"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace Concord {

/**
 * @brief Vertical/arbitrary plane reflection map for one window.
 *
 * Industrial real-time mirrors (Godot/UE planar reflections): render the scene
 * from a mirrored camera into an offscreen color target, then sample it on
 * receivers with `RenderMaterial::planarReflection`. Half-resolution by default.
 * Not a substitute for IBL probes; pairs with ordinary PBR for off-plane content.
 */
class PlanarReflection {
public:
    static constexpr float kResolutionScale = 0.5f;

    struct Targets {
        bgfx::FrameBufferHandle framebuffer = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle color = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle depth = BGFX_INVALID_HANDLE;
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        bool Valid() const noexcept { return bgfx::isValid(framebuffer); }
    };

    /** Plane in world space: point on plane + unit normal. */
    struct Plane {
        float point[3]{0.0f, 0.0f, 0.0f};
        float normal[3]{0.0f, 0.0f, 1.0f};
        bool valid = false;
    };

    void Shutdown();

    bool EnsureTargets(std::uint32_t sceneWidth, std::uint32_t sceneHeight, Targets& out) const;
    void DestroyTargets(Targets& targets) const;

    /**
     * Builds mirrored view/projection for `plane` from the main camera matrices.
     * Returns false if the plane faces away or is degenerate.
     */
    static bool BuildMirrorCamera(const float mainView[16], const float mainProj[16],
                                  const Plane& plane,
                                  float outView[16], float outProj[16],
                                  float outClipPlane[4]);
};

} // namespace Concord

#endif // CONCORD_PLANARREFLECTION_H
