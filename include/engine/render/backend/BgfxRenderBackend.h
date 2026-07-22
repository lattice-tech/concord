#ifndef CONCORD_BGFXRENDERBACKEND_H
#define CONCORD_BGFXRENDERBACKEND_H

#include "engine/render/backend/BgfxMeshPipeline.h"
#include "engine/render/backend/BgfxMeshStore.h"
#include "engine/render/backend/BgfxSceneUniforms.h"
#include "engine/render/batch/RenderBatcher.h"
#include "engine/render/debug/DebugTextOverlay.h"
#include "engine/render/environment/BgfxSkyRenderer.h"
#include "engine/render/postprocess/AntiAliasing.h"
#include "engine/render/postprocess/BgfxBloom.h"
#include "engine/render/postprocess/BgfxPostProcess.h"
#include "engine/render/postprocess/BgfxSmaa.h"
#include "engine/render/reflection/PlanarReflection.h"
#include "engine/render/reflection/ReflectionCapture.h"
#include "engine/render/shadow/ShadowConfig.h"
#include "engine/render/shadow/ShadowMap.h"
#include "engine/render/texture/BgfxTextureCache.h"
#include "engine/render/lighting/GpuLightCuller.h"
#include "engine/render/ui/UiRenderer.h"
#include "engine/render/volume/BgfxSmokeRenderer.h"
#include "engine/render/volume/BgfxVolumeCloudRenderer.h"
#include "engine/render/backend/IRenderBackend.h"
#include "engine/render/backend/RenderViewBlockAllocator.h"
#include "engine/render/mesh/MeshHandle.h"
#include "engine/render/particles/BgfxGpuParticleRenderer.h"
#include "engine/window/MsaaLevel.h"
#include "engine/window/SdlWindow.h"

#include <bgfx/bgfx.h>

#include <array>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace Concord {

/**
 * IRenderBackend implementation built on top of bgfx.
 *
 * bgfx's global state is process-wide and this backend is shared by every
 * window the engine opens, so Init brings bgfx up against a tiny hidden
 * window of its own rather than any particular Game's window. A genuinely
 * headless bgfx::init (no window at all) was tried first, but at least this
 * build's Vulkan/Direct3D renderers only advertise
 * BGFX_CAPS_SWAP_CHAIN — the capability CreateView depends on to add a
 * second window — when they were NOT initialized headless; a real (if
 * invisible) window is the only way this build supports multiple windows.
 * Every actual Game window, including the first, is then added uniformly
 * through CreateView as a secondary swap chain via bgfx::createFrameBuffer;
 * RenderView clears that window's view and Frame() presents all of them
 * together with a single bgfx::frame().
 *
 * Heavy state is split into focused collaborators kept beside this file under
 * `render/backend/` (AGENTS.md §5 - "one file, one responsibility"): `BgfxMeshStore`
 * owns mesh storage, `BgfxMeshPipeline` owns the mesh shading program, and
 * `BgfxSceneUniforms` owns the lighting + material uniform plumbing and uploads.
 * View/post-process and the directional-light shadow pass are still driven
 * here as the orchestrator that owns those collaborators.
 */
class BgfxRenderBackend final : public IRenderBackend {
public:
    BgfxRenderBackend();
    ~BgfxRenderBackend() noexcept override;

    bool Prepare(const RenderInit& init) override;
    bool Init(const RenderInit& init) override;
    RenderViewHandle CreateView(const RenderViewInit& init) override;
    void DestroyView(RenderViewHandle view) override;
    std::uint32_t NativeWindowRetirementFrames() const noexcept override;
    bool RecreateView(RenderViewHandle view, const RenderViewInit& init) override;
    MeshHandle CreateMesh(const MeshData& data) override;
    void DestroyMesh(MeshHandle mesh) override;
    void SubmitMesh(RenderViewHandle view, const MeshDrawCommand& command) override;
    void SubmitParticleEmitter(RenderViewHandle view,
                               const RenderParticleEmitter& emitter) override;
    void RenderView(RenderViewHandle view, const CameraView* camera,
                    const RenderLight* lights, std::uint32_t lightCount,
                    const SkyEnvironment* sky,
                    const RenderSmokeVolume* smokeVolumes = nullptr,
                    std::uint32_t smokeVolumeCount = 0) override;
    void Frame() override;
    BackendFrameStats Stats() const noexcept override { return m_frameStats; }
    void Shutdown() noexcept override;

private:
    static constexpr std::uint8_t kMaxFrameLatency = 2;
    static constexpr std::uint32_t kNativeWindowRetirementFrames =
        static_cast<std::uint32_t>(kMaxFrameLatency) + 2U;

    // shadow + particle compute + planar + reflection cube x6 + depth prepass + lightCull + scene +
    // cloud(march+composite) + smoke(march+composite) + SMAA x3 + bloom + present
    static constexpr std::uint32_t kViewsPerWindow =
        kShadowCascadeCount + 4 + ReflectionCapture::kFaceCount + 1
        + 2 + 2 + 3 + BgfxBloom::kMaxViews + 1;

    struct ViewSlot {
        /** The window's swap-chain framebuffer (from the native handle); the final present target. */
        bgfx::FrameBufferHandle framebuffer = BGFX_INVALID_HANDLE;
        WindowId window = kInvalidWindowId;
        void* nativeWindowHandle = nullptr;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        AntiAliasing aa = AntiAliasing::Off;

        /**
         * Post-process path, valid only when IsPostProcess(aa): the scene is
         * rendered into `scene` (an offscreen color+depth target owned by the
         * post-process pipeline), then a fullscreen pass on bgfx view
         * `presentView` samples it into the window's framebuffer.
         */
        BgfxPostProcess::Targets scene;

        /**
         * Independent half-resolution volumetric cloud pass. The march runs
         * into `cloudColor` (a half-res RGBA16F target wrapped by `cloudFb`) via
         * `cloudView`, sampling the full-res scene depth for occlusion; then
         * `cloudCompositeView` upsamples and composites it into the HDR scene
         * color before bloom/present. Both views are ordered right after the
         * scene view. The half-res target is recreated with the window on resize.
         */
        RenderViewHandle cloudView = kInvalidRenderView;
        RenderViewHandle cloudCompositeView = kInvalidRenderView;
        bgfx::TextureHandle cloudColor = BGFX_INVALID_HANDLE;
        bgfx::FrameBufferHandle cloudFb = BGFX_INVALID_HANDLE;
        std::uint32_t cloudWidth = 0;
        std::uint32_t cloudHeight = 0;

        /**
         * Local volumetric smoke. Like the clouds, the march runs at reduced
         * resolution into `smokeColor` (wrapped by `smokeFb`) via `smokeView`,
         * sampling the full-res scene depth for occlusion, then
         * `smokeCompositeView` upsamples and composites it into the HDR scene
         * color — ordered right after the cloud pass and before
         * SMAA/bloom/present. The low-res target is recreated with the window.
         */
        RenderViewHandle smokeView = kInvalidRenderView;
        RenderViewHandle smokeCompositeView = kInvalidRenderView;
        bgfx::TextureHandle smokeColor = BGFX_INVALID_HANDLE;
        /** Low-res device depth the march sampled per pixel, for the composite
         *  pass's depth-aware (bilateral) upsample; avoids blur/halo at the
         *  silhouette of foreground geometry that a naive bilinear upsample
         *  produces. Second attachment on `smokeFb` (MRT), point-sampled. */
        bgfx::TextureHandle smokeDepthProxy = BGFX_INVALID_HANDLE;
        bgfx::FrameBufferHandle smokeFb = BGFX_INVALID_HANDLE;
        std::uint32_t smokeWidth = 0;
        std::uint32_t smokeHeight = 0;

        /**
         * Extra bgfx view ids for the post-process passes, ordered after the
         * scene view so they run later. FXAA uses only `presentView` (one pass
         * to the window); SMAA additionally uses the three intermediate views
         * for its edge / weight / blend passes, then `presentView` blits the
         * result to the window.
         */
        RenderViewHandle presentView = kInvalidRenderView;
        RenderViewHandle smaaEdgeView = kInvalidRenderView;
        RenderViewHandle smaaWeightView = kInvalidRenderView;
        RenderViewHandle smaaBlendView = kInvalidRenderView;

        /**
         * HDR bloom state for this window. `bloomViews` are the view ids for
         * the mip-chain down/up passes (one per pass), all after the scene view
         * and before present; `bloom` holds the RGBA16F mip chain, recreated
         * with the window size.
         */
        std::array<RenderViewHandle, BgfxBloom::kMaxViews> bloomViews{};
        BgfxBloom::Targets bloom;

        /**
         * Stable cascaded directional-light shadows. All cascade views execute
         * before the scene view; fixed-size targets survive window rebuilds.
         */
        std::array<RenderViewHandle, kShadowCascadeCount> shadowViews{};
        std::array<bgfx::FrameBufferHandle, kShadowCascadeCount> shadowFbs{{
            BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
        std::array<bgfx::TextureHandle, kShadowCascadeCount> shadowTextures{{
            BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
        bool shadowReady = false; ///< true once the shadow RT and program are usable.

        /**
         * Planar reflection (mirrored camera). Runs before the main scene view
         * so receivers can sample the finished map. Half-res RGBA16F.
         */
        RenderViewHandle planarView = kInvalidRenderView;
        PlanarReflection::Targets planar;
        BgfxSceneUniforms::ForwardPlusContext planarForwardPlus;
        float planarViewProj[16]{};
        bool planarValid = false;

        /** GPU particle simulation dispatch; runs before all views that consume particle state. */
        RenderViewHandle particleComputeView = kInvalidRenderView;

        /** Opaque depth fill shared with the main scene framebuffer. */
        RenderViewHandle depthPrepassView = kInvalidRenderView;

        /** Forward+ GPU light-cull compute dispatch; runs before the scene view. */
        RenderViewHandle lightCullView = kInvalidRenderView;
        BgfxSceneUniforms::ForwardPlusContext mainForwardPlus;

        /** Real-time scene cubemap sampled by reflective mesh materials. */
        std::array<RenderViewHandle, ReflectionCapture::kFaceCount> reflectionViews{};
        std::array<BgfxSceneUniforms::ForwardPlusContext,
                   ReflectionCapture::kFaceCount> reflectionForwardPlus{};
        ReflectionCapture::Targets reflection;
        float reflectionProbe[3]{0.0f, 0.0f, 0.0f};
        float reflectionBoxMin[3]{0.0f, 0.0f, 0.0f};
        float reflectionBoxMax[3]{0.0f, 0.0f, 0.0f};
        std::uint64_t reflectionSignature = 0;
        bool reflectionSignatureValid = false;
        bool reflectionValid = false;
        bool reflectionBoxValid = false;

        /**
         * Cross-frame amortization of the cubemap probe. Rather than re-rendering
         * all six faces whenever the scene changes, faces refresh a few per frame
         * in round-robin order and retain their prior content between updates.
         * reflectionInitialized gates the one-time full fill so the cubemap is
         * complete before it is sampled; the pending counter tracks how many faces
         * still need refreshing under the current signature so a settled scene can
         * eventually re-cache its signature and skip the probe entirely.
         */
        std::uint32_t reflectionFaceCursor = 0;
        bool reflectionInitialized = false;
        std::uint64_t reflectionPendingSignature = 0;
        std::uint32_t reflectionPendingFaces = 0;
    };

    bool EnsureResetState(MsaaLevel level, bool vsync,
                          RenderViewHandle changedView = kInvalidRenderView,
                          const RenderViewInit* changedInit = nullptr);
    bool RecreateAllViews(RenderViewHandle changedView = kInvalidRenderView,
                          const RenderViewInit* changedInit = nullptr);
    bool CreateFramebufferForView(RenderViewHandle view, const RenderViewInit& init);

    struct ShadowPassData {
        std::array<std::array<float, 16>, kShadowCascadeCount> lightViewProj{};
        float lightDir[3]{0.0f, -1.0f, 0.0f};
        float cameraView[16]{};
        std::array<float, kShadowCascadeCount> splitDepths{};
        std::array<float, kShadowCascadeCount> blendWidths{};
        std::array<float, kShadowCascadeCount> penumbraScaleTexels{};
        std::array<float, kShadowCascadeCount> normalBiasWorld{};
        int casterIndex = -1;
        bool valid = false;
    };

    /**
     * Renders the directional-light shadow depth pass for exactly `view` into
     * the cascade framebuffers and fills `out` with sampling data for the scene pass.
     * Scoping draws to the scene view prevents geometry from another window
     * polluting this map. A no-op (`out.valid == false`) when unavailable.
     */
    void RenderShadowPass(RenderViewHandle view, ViewSlot& slot,
                          const float cameraView[16], Projection projection,
                          float fovYDegrees, float orthoHeight, float aspect,
                          float nearPlane, float farPlane,
                          const RenderLight* lights, std::uint32_t lightCount,
                          ShadowPassData& out);

    /**
     * Fills opaque depth and records only batches whose complete instance set
     * reached the prepass, allowing their color pass to use an equal-depth test.
     */
    void RenderDepthPrepass(
        ViewSlot& slot, std::span<const RenderBatch> batches,
        const std::vector<const MeshDrawCommand*>& skinned,
        std::vector<std::uint8_t>& batchResults,
        std::vector<std::uint8_t>& skinnedResults);

    /** Updates all six reflection faces around the largest reflective node. */
    void RenderReflectionCapture(RenderViewHandle ownerView, ViewSlot& slot,
                                 const std::vector<MeshDrawCommand>& commands,
                                 const std::vector<RenderParticleEmitter>* particles,
                                 const RenderLight* lights, std::uint32_t lightCount,
                                 const SkyEnvironment& environment,
                                 const RenderSmokeVolume* smokeVolumes = nullptr,
                                 std::uint32_t smokeVolumeCount = 0);

    /** Frees a view's window framebuffer and its offscreen post-process target. */
    void DestroyViewTargets(ViewSlot& slot);

    /** Frees camera-specific Forward+ textures retained across view resizes. */
    void DestroyForwardPlusContexts(ViewSlot& slot);

    /** Resolves the offscreen scene to the window via the view's AA technique (FXAA or SMAA). */
    void RunPostProcess(RenderViewHandle view, const ViewSlot& slot, bool bloomSource,
                        const ViewEffectState* effects);

    /** Draws active PrintString lines onto the window-bound present (or scene) view. */
    void DrawPrintStringOverlay(RenderViewHandle view, const ViewSlot& slot, bool postProcess);

    /** Composites the independent volumetric cloud layer into the HDR scene color. */
    void RenderVolumeClouds(ViewSlot& slot, const float viewMatrix[16],
                            const float projectionMatrix[16], const float eye[3],
                            const SkyEnvironment& environment,
                            const RenderLight* lights, std::uint32_t lightCount);

    /** Composites the local volumetric smoke volumes into the HDR scene color. */
    void RenderVolumeSmoke(ViewSlot& slot, const float viewMatrix[16],
                           const float projectionMatrix[16], const float eye[3],
                           const RenderLight* lights, std::uint32_t lightCount,
                           const RenderSmokeVolume* volumes, std::uint32_t volumeCount);

    bool m_prepared = false;
    bool m_bgfxInitialized = false;
    bool m_initialized = false;
    MsaaLevel m_activeMsaa = MsaaLevel::Off;
    bool m_activeVsync = false;
    RenderBackendType m_type = RenderBackendType::Auto;
    SdlWindow m_deviceWindow;
    std::unordered_map<RenderViewHandle, ViewSlot> m_views;
    std::unordered_map<RenderViewHandle, std::vector<MeshDrawCommand>> m_pendingDraws;
    std::unordered_map<RenderViewHandle,
                       std::vector<RenderParticleEmitter>> m_pendingParticleEmitters;
    RenderViewBlockAllocator m_viewBlocks;

    /**
     * Per-frame draw-command grouper shared by every window's RenderView.
     *
     * Kept as a single member rather than one per view because the batcher's
     * reusable storage (key map, batch list, command-pointer buffer, recycled
     * command-vector pool) is most valuable when the same instance is reused
     * across frames; BeginFrame resets its grouping between views so a view
     * never sees a previous view's batches.
     */
    RenderBatcher m_batcher;

    /** Reused per-view classification storage; RenderView calls are sequential. */
    std::vector<const MeshDrawCommand*> m_skinnedScratch;
    std::vector<MeshDrawCommand> m_shadowScratch;
    std::vector<std::uint8_t> m_depthPrepassBatchScratch;
    std::vector<std::uint8_t> m_depthPrepassSkinnedScratch;

    /** Per-command cubemap-face masks reused while rebuilding a reflection capture. */
    std::vector<std::uint8_t> m_reflectionVisibilityScratch;

    /** Uploaded-geometry store: vertex buffers, index buffers, cached local AABBs. */
    BgfxMeshStore m_meshes;

    /** Mesh shading program (vs_mesh + fs_mesh); loaded once on first use. */
    BgfxMeshPipeline m_meshProgram;

    /** Lighting + material uniform handles and per-frame/per-batch Apply. */
    BgfxSceneUniforms m_uniforms;

    /** Forward+ clustered light culler (CPU); fills the per-cluster light lists. */
    ClusteredLightCuller m_lightCuller;

    /** Forward+ GPU compute light culler; used when the device supports compute. */
    GpuLightCuller m_gpuLightCuller;

    /** Persistent compute simulation and instanced billboard rendering. */
    BgfxGpuParticleRenderer m_gpuParticles;

    /** When true (and compute is supported) culling runs on the GPU, not the CPU. */
    bool m_useGpuLightCulling = true;

    BgfxTextureCache m_textureCache;

    /** Offscreen-RT + fullscreen-pass machinery for FXAA and the final blit. */
    BgfxPostProcess m_postProcess;

    /** HDR bloom: bright-pass + separable blur, composited by the present pass. */
    BgfxBloom m_bloom;

    /** Three-pass subpixel morphological AA (SMAA). */
    BgfxSmaa m_smaa;

    /** Procedural scene/capture sky submitted before geometry in the same view. */
    BgfxSkyRenderer m_skyRenderer;

    /** Independent volumetric cloud pass, composited into the HDR scene RT. */
    BgfxVolumeCloudRenderer m_volumeCloud;

    /** Local volumetric smoke pass, composited into the HDR scene RT. */
    BgfxSmokeRenderer m_volumeSmoke;

    /** Single directional-light shadow depth pass program, target and uniforms. */
    ShadowMap m_shadowMap;

    /** Planar reflection map builder (targets per window in ViewSlot). */
    PlanarReflection m_planarReflection;
    ReflectionCapture m_reflectionCapture;

    /**
     * Bottom-left PrintString overlay. Drawn onto each Game window's swap
     * chain (not the hidden device backbuffer — see DebugTextOverlay).
     */
    DebugTextOverlay m_debugText;

    /** Draws the game UI (Concord::UI) draw list into the window overlay view. */
    UiRenderer m_uiRenderer;

    /** Shadow tunables (resolution, bias, PCF); the whole engine shares one config. */
    ShadowConfig m_shadowConfig;
    BackendFrameStats m_frameStats;
};

} // namespace Concord

#endif // CONCORD_BGFXRENDERBACKEND_H
