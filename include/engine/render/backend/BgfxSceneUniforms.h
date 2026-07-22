#ifndef CONCORD_BGFXSCENEUNIFORMS_H
#define CONCORD_BGFXSCENEUNIFORMS_H

#include "engine/render/backend/IRenderBackend.h"
#include "engine/render/frame/CameraView.h"
#include "engine/render/frame/RenderLight.h"
#include "engine/render/frame/SkyEnvironment.h"
#include "engine/render/lighting/ClusterGrid.h"
#include "engine/render/lighting/ClusteredLightCuller.h"
#include "engine/render/material/RenderMaterial.h"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace Concord {

class BgfxTextureCache;

/**
 * Owns every uniform the `fs_mesh` shading program reads, and the per-frame /
 * per-batch uploads that pack them.
 *
 * The lighting uniform set (`u_lightPosType`, `u_lightDirRange`, `u_lightColor`,
 * `u_lightSpot`, `u_ambient`, `u_camPos`) is shared by every batch a view draws
 * and is rebound before every submit through `ApplyLighting`; the material uniform set
 * (`u_albedo`, `u_gradientTo`, `u_emissive`, `u_matParams` plus the four
 * material samplers and the per-frame `u_texFlags` that signals the normal-map
     * presence, shadow-map V flip, and blend mode) changes per batch and is set through
 * `ApplyMaterial`, called inside the batch loop.
 *
 * The sampler texture binding falls back to a shared white texture held by the
 * engine's `BgfxTextureCache` when a map is absent, exactly as the inline
 * backend did — color/data maps multiply as identity, only the normal map needs
 * the dedicated "has it" flag because white is not a valid tangent-space
 * normal. Kept as its own unit (AGENTS.md §5) so the backend's submission loop
 * only drives state, not uniform plumbing.
 *
 * All methods run on the render thread.
 */
class BgfxSceneUniforms {
public:
    /** Creates every uniform the program reads. Idempotent. */
    void EnsureReady();

    /** Frees every uniform. Safe when never ready or after a previous Shutdown. */
    void Shutdown();

    /** True once the uniforms are live and ready for `Apply*` calls. */
    bool Ready() const noexcept { return m_initialized; }

    /**
     * Packs this frame's lights into the parallel vec4 arrays the shader
     * unpacks, plus the ambient fill (with the active light count in the
     * w slot) and the camera eye position. Uploads the full light arrays
     * every call (the tail past `count` is zero-filled) so no light uniform
     * is ever left unset, even in a scene with no lights.
     */
    void ApplyLighting(const CameraView* camera, const RenderLight* lights,
                       std::uint32_t lightCount, const SkyEnvironment* sky = nullptr);

    /**
     * Binds every material sampler and uploads the resolved material colors
     * and parameters for one batch. Absent color/data maps fall back to the
     * shared white texture inside `textures`; only the normal map needs a
     * dedicated "has it" flag inside `u_texFlags`. `flipV` is the shadow-map
     * V-flip flag, threaded to the shader through `u_texFlags.y`; the resolved
     * blend mode is packed in `u_texFlags.z` for effect shaders. `linearOutput`
     * bypasses display tone mapping for HDR reflection-capture passes. A valid
     * `sceneReflection` is sampled only when `realtimeReflection` is true. The
     * optional probe and box vectors enable stable box-projected local parallax.
     * `clipPlane` is an optional world-space equation; fragments on its negative
     * side are discarded for planar reflection captures.
     */
    void ApplyMaterial(const RenderMaterial& material, BgfxTextureCache& textures, bool flipV,
                       bgfx::TextureHandle planarReflection = BGFX_INVALID_HANDLE,
                       const float planarViewProj[16] = nullptr,
                       bool linearOutput = false,
                       bgfx::TextureHandle sceneReflection = BGFX_INVALID_HANDLE,
                       const float reflectionProbe[3] = nullptr,
                        const float reflectionBoxMin[3] = nullptr,
                        const float reflectionBoxMax[3] = nullptr,
                        bool realtimeReflection = false,
                        const float clipPlane[4] = nullptr);

    /**
     * Uploads a skinning matrix palette to `u_bones` for a skinned draw.
     * @param palette Pointer to `count` column-major 4x4 matrices (16 floats each).
     * @param count Number of bones; clamped to kMaxRenderBones.
     * Only meaningful for the skinned mesh program (vs_mesh_skinned).
     */
    void BindBones(const float* palette, std::uint32_t count);

    /**
     * Uploads the Forward+ clustered light data (packed light list, per-cluster
     * ranges, flat index list) into the data textures `fs_mesh` samples, and
     * sets the cluster params (mode, directional count, near, far) applied by
     * every subsequent `ApplyMaterial`. When `enabled` is false the mode is set
     * to classic so the shader uses the fixed 8-light uniform path (fallback),
     * and the reflection-capture pass always runs classic.
     */
    void UpdateClusters(const ClusteredLightCuller& culler, const ClusterGrid& grid, bool enabled);

    /**
     * Uploads only the packed light-data texture from `culler.Lights()` (used
     * ahead of the GPU compute culler, which reads that texture) and points the
     * cluster-range/light-index samplers at the GPU-produced `rangeTex`/
     * `indexTex` instead of the CPU-owned ones, for the rest of this frame's
     * `ApplyMaterial` calls. Falls back to `UpdateClusters`'s CPU textures if
     * either handle is invalid.
     */
    void UpdateClustersGpu(const ClusteredLightCuller& culler, const ClusterGrid& grid,
                           bgfx::TextureHandle rangeTex, bgfx::TextureHandle indexTex);

    /** The CPU-uploaded packed light-data texture the GPU compute culler reads. */
    bgfx::TextureHandle LightDataTexture() const noexcept { return m_lightDataTex; }

private:
    bool m_initialized = false;

    // Forward+ clustered lighting data textures + params (fs_mesh stages 9-11).
    bgfx::UniformHandle m_sLightData = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sClusterRanges = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sLightIndices = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uClusterParams = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_lightDataTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_clusterRangeTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_lightIndexTex = BGFX_INVALID_HANDLE;
    float m_clusterParams[4] = {0.0f, 0.0f, 0.1f, 200.0f}; // mode, dirCount, near, far

    /** Range/index textures actually bound this frame: CPU-owned by default,
     *  or the GPU compute culler's output when UpdateClustersGpu selected it. */
    bgfx::TextureHandle m_activeRangeTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_activeIndexTex = BGFX_INVALID_HANDLE;

    bgfx::UniformHandle m_uAlbedo = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uGradientTo = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uEmissive = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uMatParams = BGFX_INVALID_HANDLE;

    bgfx::UniformHandle m_uLightPosType = BGFX_INVALID_HANDLE;  // xyz world pos, w type
    bgfx::UniformHandle m_uLightDirRange = BGFX_INVALID_HANDLE; // xyz world dir, w range
    bgfx::UniformHandle m_uLightColor = BGFX_INVALID_HANDLE;    // rgb color*intensity, w unused
    bgfx::UniformHandle m_uLightSpot = BGFX_INVALID_HANDLE;     // x cosInner, y cosOuter
    bgfx::UniformHandle m_uAmbient = BGFX_INVALID_HANDLE;       // rgb ambient, w active light count
    bgfx::UniformHandle m_uCamPos = BGFX_INVALID_HANDLE;        // xyz eye position

    bgfx::UniformHandle m_sAlbedo = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sNormal = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sMetallicRoughness = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sEmissive = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uTexFlags = BGFX_INVALID_HANDLE; // x normal, y shadow V, z blend, w planar
    bgfx::UniformHandle m_uOutputFlags = BGFX_INVALID_HANDLE; // x linear HDR output

    bgfx::UniformHandle m_uBones = BGFX_INVALID_HANDLE; // mat4[kMaxRenderBones] skinning palette

    bgfx::UniformHandle m_sPlanarReflection = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uPlanarViewProj = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sSceneReflection = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uReflectionFlags = BGFX_INVALID_HANDLE; // x enabled, y reflectivity
    bgfx::UniformHandle m_uReflectionProbe = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uReflectionBoxMin = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uReflectionBoxMax = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uClipPlane = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_fallbackReflectionCube = BGFX_INVALID_HANDLE;
};

} // namespace Concord

#endif // CONCORD_BGFXSCENEUNIFORMS_H
