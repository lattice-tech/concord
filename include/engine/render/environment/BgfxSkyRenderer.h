#ifndef CONCORD_BGFXSKYRENDERER_H
#define CONCORD_BGFXSKYRENDERER_H

#include "engine/render/backend/IRenderBackend.h"
#include "engine/render/frame/RenderLight.h"
#include "engine/render/frame/SkyEnvironment.h"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace Concord {

/**
 * Draws a procedural, camera-oriented sky before scene geometry.
 *
 * The renderer submits into an existing sequential scene/capture view and
 * does not allocate another bgfx view. It writes color only, leaving the
 * cleared depth buffer untouched so later geometry naturally covers it.
 */
class BgfxSkyRenderer {
public:
    /** Lazily creates the sky program, uniforms and fullscreen triangle. */
    bool EnsureReady();

    /** Releases every GPU resource; safe before initialization and after shutdown. */
    void Shutdown();

    /**
     * Draws one sky into @p view.
     *
     * @param viewMatrix World-to-view matrix for this scene or capture face.
     * @param projectionMatrix Projection paired with @p viewMatrix.
     * @param eye Camera position used to reconstruct world-space view rays.
     * @param environment Authored sky colors and intensity.
     * @param lights Frame light list; the first directional light supplies the sun disk.
     * @param lightCount Number of entries in @p lights.
     * @param linearOutput True for HDR reflection captures, false for display-referred scenes.
     * @param drawClouds When false, suppresses the inline sky cloud march so the
     *        independent volumetric cloud pass owns clouds for that view.
     */
    void Draw(RenderViewHandle view, const float viewMatrix[16],
              const float projectionMatrix[16], const float eye[3],
              const SkyEnvironment& environment,
              const RenderLight* lights, std::uint32_t lightCount,
              bool linearOutput = false, bool drawClouds = true);

private:
    void DestroyResources();

    bool m_ready = false;
    bool m_attempted = false;
    bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uInvViewProj = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uCamera = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uSolid = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uZenith = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uHorizon = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uGround = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uOptions = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uSunDirection = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uSunColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uCloudLayer = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uCloudShape = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uCloudMotion = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uCloudLit = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uCloudShadow = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uCloudFire = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uFogParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uFogColor = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_fullscreenVb = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout m_layout;
};

} // namespace Concord

#endif // CONCORD_BGFXSKYRENDERER_H
