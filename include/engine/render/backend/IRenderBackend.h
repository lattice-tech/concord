#ifndef CONCORD_IRENDERBACKEND_H
#define CONCORD_IRENDERBACKEND_H

#include "engine/render/postprocess/AntiAliasing.h"
#include "engine/render/frame/CameraView.h"
#include "engine/render/mesh/MeshData.h"
#include "engine/render/mesh/MeshHandle.h"
#include "engine/render/backend/RenderBackendType.h"
#include "engine/render/frame/RenderLight.h"
#include "engine/render/frame/RenderParticleEmitter.h"
#include "engine/render/frame/RenderSmokeVolume.h"
#include "engine/render/frame/RenderEffect.h"
#include "engine/render/frame/SkyEnvironment.h"
#include "engine/render/material/RenderMaterial.h"
#include "engine/window/MsaaLevel.h"
#include "engine/window/WindowId.h"

#include <cstdint>
#include <array>

namespace Concord {

/** Opaque handle to one window's render target inside a backend. */
using RenderViewHandle = std::uint16_t;

/** Sentinel returned by CreateView on failure; never a handle actually in use. */
inline constexpr RenderViewHandle kInvalidRenderView = 0xFFFF;

/** Maximum bones in a skinning palette; must match `u_bones[]` in vs_mesh_skinned. */
inline constexpr std::uint32_t kMaxRenderBones = 64;

/** Everything a backend needs to bring up the process-wide graphics device. */
struct RenderInit {
    /** Graphics API to target; Auto lets the backend pick the platform default. */
    RenderBackendType type = RenderBackendType::Auto;
};

/** Everything a backend needs to add one more window as a render target. */
struct RenderViewInit {
    /** Public window identity used to isolate per-window overlays and input. */
    WindowId window = kInvalidWindowId;

    /** Native OS window handle (HWND on Windows) to render into. */
    void* nativeWindowHandle = nullptr;

    /** Backbuffer size, in pixels. */
    std::uint32_t width = 1280;
    std::uint32_t height = 720;

    /** Requested multisample level for the process-wide swap chain. */
    MsaaLevel msaa = MsaaLevel::X4;

    /**
     * The full anti-aliasing technique. Every mode normally renders through
     * the RGBA16F offscreen target and final dithered present. FXAA/SMAA add
     * their fullscreen passes. MSAA modes remain experimental until that HDR
     * target is multisampled and explicitly resolved.
     */
    AntiAliasing aa = AntiAliasing::Off;

    /** Whether presentation waits for vertical refresh (process-wide in bgfx). */
    bool vsync = true;
};

/** Per-draw data for one mesh instance submitted before RenderView. */
struct MeshDrawCommand {
    /** Which mesh to draw; must come from this backend's CreateMesh. */
    MeshHandle mesh;

    /** Column-major 4x4 world matrix (Concord Transform + size baked in). */
    float worldMatrix[16]{};

    /** Resolved surface appearance, packed per-instance for the mesh shader. */
    RenderMaterial material{};

    /** Shader path used for this draw. */
    RenderEffect effect = RenderEffect::Mesh;

    /**
     * When true this draw keeps its rasterized mesh but samples the backend's
     * current real-time scene reflection capture (see RenderInstance::rayTraced).
     * Defaults false, so callers that never opt in are unaffected.
     */
    bool rayTraced = false;

    /**
     * Equality-only owner key for the reflective node which produced this
     * command. Zero preserves legacy direct-backend behavior, where each
     * command is treated as an independent receiver.
     */
    std::uintptr_t reflectionOwner = 0;

    /**
     * Skinning matrix palette: `boneCount` column-major 4x4 matrices (16 floats
     * each). When non-null the draw takes the skinned mesh path (vs_mesh_skinned
     * + the bone palette uploaded to `u_bones`) instead of the rigid batch path.
     * The pointer must stay valid until RenderView consumes this frame's draws;
     * a SkinnedModel keeps its palette alive for the frame. Null for static meshes.
     */
    const float* bonePalette = nullptr;
    std::uint32_t boneCount = 0;
};

/** Backend-neutral measurements copied at the end of a submitted frame. */
struct BackendFrameStats {
    float gpuFrameMs = 0.0f;
    std::uint32_t drawCalls = 0;
    std::uint32_t computeCalls = 0;
    std::uint32_t gpuFrame = 0;
    std::uint32_t textureCount = 0;
    std::uint64_t textureMemoryBytes = 0;
    std::uint64_t renderTargetMemoryBytes = 0;
    std::uint32_t transientVertexBytes = 0;
    std::uint32_t transientIndexBytes = 0;
    bool valid = false;
    std::uint32_t passCount = 0;
    float slowestPassMs = 0.0f;
    std::array<char, 64> slowestPass{};
};

/**
 * Abstract contract for a rendering backend owned by the engine's shared
 * render thread.
 *
 * Concrete implementations wrap a specific graphics API (e.g. bgfx) behind
 * this interface so the active rendering technology can be replaced later
 * without changing any code that only depends on this contract. The backend
 * itself is process-wide (Prepare/Init bring up one graphics device with no
 * window bound); each window the engine opens is then registered
 * independently through CreateView, so multiple windows share one device
 * instead of each requiring its own. Every method is expected to run on the
 * engine's render thread.
 */
class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    /**
     * Validates and stages process-local state before graphics initialization.
     *
     * This hook must not call the graphics API's global initialization entry
     * point. It exists so future backends can prepare adapters, allocators or
     * diagnostics before Init performs the irreversible initialization.
     */
    virtual bool Prepare(const RenderInit& init) = 0;

    /**
     * Brings the process-wide graphics device up with no window bound yet.
     * @return true on success, false if the backend failed to initialize.
     */
    virtual bool Init(const RenderInit& init) = 0;

    /**
     * Registers one more window as a render target.
     * @param init Target window handle and backbuffer size.
     * @return A handle to pass to RenderView/DestroyView, or kInvalidRenderView on failure.
     */
    virtual RenderViewHandle CreateView(const RenderViewInit& init) = 0;

    /** Releases the render target created by CreateView. Safe to call once per handle. */
    virtual void DestroyView(RenderViewHandle view) = 0;

    /**
     * Number of completed Frame calls required before a native window passed
     * to a destroyed view may itself be destroyed.
     *
     * Backends with an asynchronous render thread must cover their maximum
     * command latency. A synchronous backend may return zero.
     */
    virtual std::uint32_t NativeWindowRetirementFrames() const noexcept = 0;

    /**
     * Uploads `data` into GPU buffers and returns a handle naming them.
     * @return A handle to pass to SubmitMesh/DestroyMesh, or an invalid
     *         handle if the backend is not initialized or `data` is empty.
     */
    virtual MeshHandle CreateMesh(const MeshData& data) = 0;

    /** Releases the GPU buffers named by `mesh`. A no-op if `mesh` is stale/invalid. */
    virtual void DestroyMesh(MeshHandle mesh) = 0;

    /**
     * Rebuilds an existing view's framebuffer after a live size or MSAA change.
     * @return true if the view was found and recreated successfully.
     */
    virtual bool RecreateView(RenderViewHandle view, const RenderViewInit& init) = 0;

    /**
     * Queues one mesh draw for `view`, to be executed when RenderView is
     * called for that same handle this frame. May be called multiple times
     * per view per frame (one call per mesh instance).
     */
    virtual void SubmitMesh(RenderViewHandle view, const MeshDrawCommand& command) = 0;

    /**
     * Queues one GPU-simulated particle emitter for `view`.
     *
     * The default no-op keeps custom backends source-compatible; backends that
     * advertise GPU particle support consume the cumulative sequence/time fields
     * to update persistent per-emitter device state.
     */
    virtual void SubmitParticleEmitter(RenderViewHandle view,
                                       const RenderParticleEmitter& emitter)
    {
        (void)view;
        (void)emitter;
    }

    /**
     * Draws every mesh submitted to `view` this frame, then clears the view.
     * @param camera View/projection to render with; when null the backend uses
     *        its own default framing. The projection matrix is built here from
     *        the camera's parameters and this view's live aspect ratio.
     * @param lights Pointer to this frame's lights for the view (may be null
     *        when `lightCount` is zero); the shading pass accumulates up to
     *        kMaxRenderLights of them and falls back to ambient for the rest.
     * @param lightCount Number of valid entries in `lights`.
     * @param sky Scene-level background and indirect-light settings; null uses defaults.
     * @param smokeVolumes Pointer to this frame's local smoke volumes for the
     *        view (may be null when `smokeVolumeCount` is zero); composited into
     *        the HDR scene color after clouds, up to kMaxRenderSmokeVolumes.
     * @param smokeVolumeCount Number of valid entries in `smokeVolumes`.
     */
    virtual void RenderView(RenderViewHandle view, const CameraView* camera,
                            const RenderLight* lights, std::uint32_t lightCount,
                            const SkyEnvironment* sky,
                            const RenderSmokeVolume* smokeVolumes = nullptr,
                            std::uint32_t smokeVolumeCount = 0) = 0;

    /** Presents every view rendered this tick. Called once per render-loop iteration. */
    virtual void Frame() = 0;

    /** Returns the most recently copied backend frame measurements. */
    virtual BackendFrameStats Stats() const noexcept = 0;

    /**
     * Releases every resource the backend acquired during Init(), including
     * any views that were never explicitly destroyed.
     * Must be safe to call even if Init() was never called or failed, and must
     * not return until the backend no longer references native window handles.
     */
    virtual void Shutdown() noexcept = 0;
};

} // namespace Concord

#endif // CONCORD_IRENDERBACKEND_H
