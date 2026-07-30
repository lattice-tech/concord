#ifndef CONCORD_BGFXWATERRENDERER_H
#define CONCORD_BGFXWATERRENDERER_H

#include "engine/render/backend/IRenderBackend.h"
#include "engine/render/frame/RenderLight.h"
#include "engine/render/frame/RenderWaterSurface.h"
#include "engine/render/frame/SkyEnvironment.h"
#include "engine/render/water/WaterCascade.h"
#include "engine/render/water/WaterClipmapMesh.h"
#include "engine/render/water/WaterGridMesh.h"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace Concord {

/**
 * Draws water surfaces as displaced, Fresnel-shaded geometry.
 *
 * Each surface is one draw of a shared unit grid (see WaterGridMesh) that
 * `vs_water` displaces with the same Gerstner sum the CPU evaluates, so buoyancy
 * queries and the drawn surface never disagree. `fs_water` blends the body
 * colour toward the sky along Schlick's Fresnel term, absorbs light over the
 * view-angle-dependent path through the authored depth, adds a GGX sun glint and
 * tears whitecaps out of the steepest crests.
 *
 * The pass draws into the window's HDR scene target with depth testing and depth
 * writes on, ordered after opaque geometry and particles but before the cloud
 * and smoke composites, so those still occlude correctly. Transparency comes
 * from alpha blending against the already-resolved opaque scene rather than from
 * a colour copy: reading the target it draws into would need a blit, which is
 * what the later screen-space refraction phase adds.
 *
 * Owns only shared GPU resources (program, uniforms, grids); the view id and
 * framebuffer belong to the backend. All methods run on the render thread.
 */
class BgfxWaterRenderer {
public:
    /** Lazily creates the water program, uniforms and vertex layout. */
    bool EnsureReady();

    /** Releases every GPU resource; safe before initialization and after shutdown. */
    void Shutdown();

    /** Inputs for one window's water pass; every handle is owned by the caller. */
    struct DrawParams {
        RenderViewHandle view = kInvalidRenderView;            ///< Water view id.
        /** Cascade bake view; must be ordered before `view`. */
        RenderViewHandle bakeView = kInvalidRenderView;
        bgfx::FrameBufferHandle sceneFb = BGFX_INVALID_HANDLE;  ///< HDR scene target.
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        const float* viewMatrix = nullptr;
        const float* projectionMatrix = nullptr;
        const float* eye = nullptr;
        const RenderLight* lights = nullptr;
        std::uint32_t lightCount = 0;

        /** Resolved scene colour/depth to copy from, and the copies to sample. */
        bgfx::TextureHandle sceneColor = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle sceneDepth = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle sceneColorCopy = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle sceneDepthCopy = BGFX_INVALID_HANDLE;

        /** Camera clip range, needed to turn sampled device depth into metres. */
        float nearPlane = 0.1f;
        float farPlane = 100.0f;

        /**
         * Planar reflection of the real scene, rendered from a camera mirrored
         * across this water plane, plus the view-projection to look it up with.
         * Invalid when the pass did not run, which falls the surface back to the
         * analytic sky reflection.
         */
        bgfx::TextureHandle planarColor = BGFX_INVALID_HANDLE;
        const float* planarViewProj = nullptr;
        bool flipPlanarV = false;
    };

    /**
     * Draws up to kMaxRenderWaterSurfaces surfaces; extra surfaces are dropped
     * with a diagnostic. A no-op when the pass is unavailable or there is
     * nothing to draw, leaving the scene target untouched.
     */
    void Draw(const DrawParams& params, const SkyEnvironment& environment,
              const RenderWaterSurface* surfaces, std::uint32_t surfaceCount);

private:
    /** Uploads the per-surface uniforms shared by the vertex and fragment stages. */
    void ApplySurface(const RenderWaterSurface& surface,
                      const SkyEnvironment& environment,
                      const DrawParams& params);

    void DestroyResources();

    bool m_ready = false;
    bool m_attempted = false;
    bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uSurface = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uFlow = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uCamera = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uShallow = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uDeep = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uOptics = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uSunDir = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uSunColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uSkyZenith = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uSkyHorizon = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uAmbient = BGFX_INVALID_HANDLE;
    /** x = near, y = far, z = 1 when the backend uses [-1,1] clip depth, w = refraction on. */
    bgfx::UniformHandle m_uDepthParams = BGFX_INVALID_HANDLE;
    /** x = SSS strength, y = foam grain scale, z = sun glint intensity, w = packed scattering tint. */
    bgfx::UniformHandle m_uAdvanced = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sSceneColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sSceneDepth = BGFX_INVALID_HANDLE;
    /** Planar reflection lookup: matrix, its enable/flip flags and the map. */
    bgfx::UniformHandle m_uPlanarViewProj = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uPlanarParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sPlanar = BGFX_INVALID_HANDLE;
    /** Per-level cascade mapping and the per-tile placement, for the vertex stage. */
    bgfx::UniformHandle m_uCascadeParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uTile = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sCascade = BGFX_INVALID_HANDLE;
    WaterCascade m_cascade;
    WaterClipmapMesh m_clipmap;
    WaterGridMesh m_grids;
};

} // namespace Concord

#endif // CONCORD_BGFXWATERRENDERER_H
