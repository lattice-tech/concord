#ifndef CONCORD_BGFXFLUIDRENDERER_H
#define CONCORD_BGFXFLUIDRENDERER_H

#include "engine/render/backend/IRenderBackend.h"
#include "engine/render/fluid/FluidGpuState.h"
#include "engine/render/frame/RenderFluid.h"
#include "engine/render/frame/RenderLight.h"
#include "engine/render/frame/SkyEnvironment.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Concord {

/**
 * @brief GPU DFSPH fluid pipeline: simulate, reconstruct, refract.
 *
 * Per fluid body per frame, on the compute view: spatial-hash neighbor
 * search, then DFSPH substeps — non-pressure forces, the divergence-free
 * velocity solve AND the constant-density solve (both, always; that pairing
 * is what makes the water incompressible, unlike WCSPH) — then the smoothed
 * density field and a sparse Marching Cubes surface mesh (only iso-crossing
 * voxels emit triangles; no metaballs, and the temporal field blend kills
 * frame-to-frame surface jitter).
 *
 * On the draw view the surface is shaded with true dual-interface
 * refraction: Snell at entry, a ray march through the very density field the
 * mesh was cut from, Snell again at exit (with TIR bounces), and a
 * depth-guided march of the final ray against the scene — the convex-lens
 * warping comes from the water's actual shape, not a UV offset.
 *
 * All methods run on the process render thread.
 */
class BgfxFluidRenderer {
public:
    BgfxFluidRenderer() = default;
    BgfxFluidRenderer(const BgfxFluidRenderer&) = delete;
    BgfxFluidRenderer& operator=(const BgfxFluidRenderer&) = delete;

    /** Returns whether the active bgfx device supports compute. */
    bool Supported() const;

    /** Creates all programs, uniform handles and the MC table buffer. */
    bool EnsureReady();

    /** Advances every submitted fluid on `computeView` for this frame. */
    void Simulate(RenderViewHandle ownerView, RenderViewHandle computeView,
                  const RenderFluid* fluids, std::uint32_t fluidCount);

    /** Inputs for the surface draw; every handle is owned by the caller. */
    struct DrawParams {
        RenderViewHandle ownerView = kInvalidRenderView;
        RenderViewHandle view = kInvalidRenderView;
        bgfx::FrameBufferHandle sceneFb = BGFX_INVALID_HANDLE;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        const float* viewMatrix = nullptr;
        const float* projectionMatrix = nullptr;
        const float* eye = nullptr;
        const RenderLight* lights = nullptr;
        std::uint32_t lightCount = 0;
        bgfx::TextureHandle sceneColor = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle sceneDepth = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle sceneColorCopy = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle sceneDepthCopy = BGFX_INVALID_HANDLE;
        float nearPlane = 0.1f;
        float farPlane = 100.0f;
    };

    /** Draws the reconstructed surfaces with dual-interface refraction. */
    void Draw(const DrawParams& params, const SkyEnvironment& environment,
              const RenderFluid* fluids, std::uint32_t fluidCount);

    /** Releases bodies associated with a destroyed window view. */
    void DestroyView(RenderViewHandle ownerView);

    /** Retires bodies that have not appeared in recent snapshots. */
    void EndFrame();

    /** Releases all shared and per-body GPU resources. */
    void Shutdown();

    /** True after shared GPU resources have been created successfully. */
    bool Ready() const noexcept { return m_ready; }

private:
    struct FluidKey {
        RenderViewHandle ownerView = kInvalidRenderView;
        std::uintptr_t fluidKey = 0;
        bool operator==(const FluidKey& other) const noexcept
        {
            return ownerView == other.ownerView && fluidKey == other.fluidKey;
        }
    };

    struct FluidKeyHash {
        std::size_t operator()(const FluidKey& key) const noexcept;
    };

    static constexpr std::uint32_t kProgramCount = 15;

    FluidGpuState& EnsureState(const FluidKey& key, const RenderFluid& fluid);
    void RunBody(RenderViewHandle computeView, const RenderFluid& fluid,
                 FluidGpuState& state);
    void RunSimulation(RenderViewHandle computeView, const RenderFluid& fluid,
                       FluidGpuState& state, float subDt, float maxSpeed,
                       bool reset);
    void RunReconstruction(RenderViewHandle computeView, const RenderFluid& fluid,
                           FluidGpuState& state, std::uint32_t flags);
    void BindParams(const RenderFluid& fluid, float dt, std::uint32_t flags,
                    float maxSpeed) const;
    void BindGrid(const FluidGpuState& state) const;
    void BindField(const FluidGpuState& state) const;
    void Dispatch(std::uint32_t view, bgfx::ProgramHandle program,
                  std::uint32_t threads) const;

    bool m_ready = false;
    bool m_attempted = false;
    bool m_loggedDrawActive = false;
    std::uint64_t m_frameNumber = 1;
    bgfx::ProgramHandle m_programs[kProgramCount]{};
    bgfx::ProgramHandle m_drawProgram = BGFX_INVALID_HANDLE;

    bgfx::UniformHandle m_uParams = BGFX_INVALID_HANDLE;      ///< vec4 x 16.
    bgfx::UniformHandle m_sFieldTex = BGFX_INVALID_HANDLE;    ///< sampler3d.
    bgfx::UniformHandle m_uEye = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uScreen = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uSunDir = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uSunColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uSkyZenith = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uSkyHorizon = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uOptics = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uField0 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uField1 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uInvModel = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_uModel = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sCounterTex = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sSceneColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sSceneDepth = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sField3d = BGFX_INVALID_HANDLE;

    /** Read-only canonical Marching Cubes table (256 x 16 ints). */
    bgfx::VertexBufferHandle m_triTable = BGFX_INVALID_HANDLE;

    std::unordered_map<FluidKey, FluidGpuState, FluidKeyHash> m_bodies;
};

} // namespace Concord

#endif // CONCORD_BGFXFLUIDRENDERER_H
