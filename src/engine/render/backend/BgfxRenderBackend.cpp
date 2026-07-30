#include "engine/render/backend/BgfxRenderBackend.h"

#include "engine/debug/Logger.h"
#include "engine/particles/ParticleSimulationRuntime.h"
#include "engine/render/backend/BgfxLogSink.h"
#include "engine/render/backend/BgfxMathConverters.h"
#include "engine/utils/PrintString.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace {

namespace Detail = Concord::RenderDetail;
using Detail::ToBgfxResetFlags;
using Detail::ToBgfxRenderer;
using Detail::LogSinkInstance;

} // namespace

namespace Concord {

BgfxRenderBackend::BgfxRenderBackend() = default;

BgfxRenderBackend::~BgfxRenderBackend() noexcept
{
    Shutdown();
}

bool BgfxRenderBackend::Prepare(const RenderInit& init)
{
    if (m_bgfxInitialized || m_initialized) {
        return false;
    }

    m_type = init.type;
    m_prepared = true;
    return true;
}

bool BgfxRenderBackend::Init(const RenderInit& init)
{
    if (m_initialized) {
        return true;
    }
    if (!m_prepared || init.type != m_type) {
        Debug::Logger::Error("Render", "Prepare must succeed before Init");
        return false;
    }
    Particles::ParticleSimulationRuntime::SetGpuAvailability(
        Particles::GpuParticleAvailability::Unavailable);

    // A genuinely headless bgfx::init (platformData.nwh left null) does
    // succeed on this build, but it also strips BGFX_CAPS_SWAP_CHAIN from
    // the renderer's capabilities — the very thing CreateView needs to add
    // a window afterwards — so it cannot be used here. This hidden window
    // exists purely to give bgfx a real (if invisible) native handle to
    // create its graphics device against; its own backbuffer is never
    // presented, since every Game window is added afterwards as its own
    // secondary swap chain through CreateView.
    if (!m_deviceWindow.Open("Concord (internal)", 8, 8, /*visible=*/false)) {
        Debug::Logger::Error("Render", "could not create the internal device window");
        return false;
    }

    bgfx::Init initInfo;
    initInfo.type = ToBgfxRenderer(m_type);
    initInfo.callback = &LogSinkInstance();
    initInfo.platformData.nwh = m_deviceWindow.NativeHandle();
    initInfo.resolution.width = 8;
    initInfo.resolution.height = 8;
    initInfo.resolution.reset = BGFX_RESET_NONE;
    initInfo.resolution.maxFrameLatency = kMaxFrameLatency;

    if (!bgfx::init(initInfo)) {
        Debug::Logger::Error("Render", "bgfx::init failed (Vulkan required)");
        m_deviceWindow.Close();
        m_prepared = false;
        return false;
    }
    m_bgfxInitialized = true;

    // Hard gate: Concord embeds SPIR-V shaders only. Never run on D3D/GL.
    if (bgfx::getRendererType() != bgfx::RendererType::Vulkan) {
        Debug::Logger::Error(
            "Render",
            "engine is Vulkan-only; bgfx came up as %s — refusing to continue",
            bgfx::getRendererName(bgfx::getRendererType()));
        bgfx::shutdown();
        m_bgfxInitialized = false;
        m_deviceWindow.Close();
        m_prepared = false;
        return false;
    }

    const std::uint32_t requiredCaps = BGFX_CAPS_SWAP_CHAIN | BGFX_CAPS_INSTANCING;
    if ((bgfx::getCaps()->supported & requiredCaps) != requiredCaps) {
        Debug::Logger::Error("Render",
                             "Vulkan lacks a required capability (multi-window swap chain and/or instancing)");
        bgfx::shutdown();
        m_bgfxInitialized = false;
        m_deviceWindow.Close();
        m_prepared = false;
        return false;
    }

    // The mesh store's layout can be set up at Init time (CreateMesh may run
    // before the first SubmitMesh): it builds the position+normal+UV vertex
    // layout every upload copies against.
    m_meshes.InitLayout();
    m_viewBlocks.Configure(kViewsPerWindow, bgfx::getCaps()->limits.maxViews,
                           kInvalidRenderView);
    bgfx::setDebug(BGFX_DEBUG_PROFILER);

    const bool gpuParticlesReady = m_gpuParticles.EnsureReady();
    Particles::ParticleSimulationRuntime::SetGpuAvailability(
        gpuParticlesReady ? Particles::GpuParticleAvailability::Available
                          : Particles::GpuParticleAvailability::Unavailable);

    m_initialized = true;
    Debug::Logger::Info("Render", "requested %s, using %s",
                        ToString(m_type), bgfx::getRendererName(bgfx::getRendererType()));
    return true;
}

bool BgfxRenderBackend::EnsureResetState(MsaaLevel level, bool vsync,
                                         RenderViewHandle changedView,
                                         const RenderViewInit* changedInit)
{
    if (m_activeMsaa == level && m_activeVsync == vsync) {
        return true;
    }
    const MsaaLevel previousMsaa = m_activeMsaa;
    const bool previousVsync = m_activeVsync;
    const bool msaaChanged = previousMsaa != level;
    std::vector<std::pair<RenderViewHandle, RenderViewInit>> previousViews;
    if (msaaChanged) {
        previousViews.reserve(m_views.size());
        for (const auto& [view, slot] : m_views) {
            RenderViewInit oldInit;
            oldInit.window = slot.window;
            oldInit.nativeWindowHandle = slot.nativeWindowHandle;
            oldInit.width = slot.width;
            oldInit.height = slot.height;
            oldInit.msaa = previousMsaa;
            oldInit.aa = slot.aa;
            oldInit.vsync = previousVsync;
            previousViews.emplace_back(view, oldInit);
        }
    }
    m_activeMsaa = level;
    m_activeVsync = vsync;
    // MSAA and vsync are both process-wide bgfx::reset state on the internal
    // device backbuffer; the flags below are re-applied together on any change.
    bgfx::reset(8, 8, ToBgfxResetFlags(level) | (vsync ? BGFX_RESET_VSYNC : 0));
    // Only MSAA changes the per-window framebuffer format, so vsync-only
    // changes skip the (costly) rebuild of every view.
    if (msaaChanged) {
        if (RecreateAllViews(changedView, changedInit)) {
            return true;
        }

        Debug::Logger::Error("Render", "view rebuild failed; restoring previous reset state");
        m_activeMsaa = previousMsaa;
        m_activeVsync = previousVsync;
        bgfx::reset(8, 8, ToBgfxResetFlags(previousMsaa)
            | (previousVsync ? BGFX_RESET_VSYNC : 0));
        bool restored = true;
        for (const auto& [view, oldInit] : previousViews) {
            const auto it = m_views.find(view);
            if (it == m_views.end()) {
                restored = false;
                continue;
            }
            const ViewSlot currentTargets = it->second;
            if (!CreateFramebufferForView(view, oldInit)) {
                restored = false;
                continue;
            }
            ViewSlot retired = currentTargets;
            DestroyViewTargets(retired);
        }
        if (!restored) {
            Debug::Logger::Error("Render", "failed to restore one or more views after reset rollback");
        }
        return false;
    }
    return true;
}

bool BgfxRenderBackend::RecreateAllViews(RenderViewHandle changedView,
                                         const RenderViewInit* changedInit)
{
    bool allSucceeded = true;
    for (auto& [view, slot] : m_views) {
        if (slot.nativeWindowHandle == nullptr) {
            continue;
        }

        RenderViewInit init = changedInit != nullptr && view == changedView
            ? *changedInit
            : RenderViewInit{};
        if (changedInit == nullptr || view != changedView) {
            init.window = slot.window;
            init.nativeWindowHandle = slot.nativeWindowHandle;
            init.width = slot.width;
            init.height = slot.height;
            init.aa = slot.aa;
        }
        init.msaa = m_activeMsaa;
        const ViewSlot oldTargets = slot;
        if (!CreateFramebufferForView(view, init)) {
            Debug::Logger::Error("Render", "failed to recreate view %u after MSAA change", view);
            allSucceeded = false;
        } else {
            ViewSlot retired = oldTargets;
            DestroyViewTargets(retired);
        }
    }
    return allSucceeded;
}

void BgfxRenderBackend::Frame()
{
    if (!m_initialized) {
        return;
    }
    bgfx::frame();
    m_gpuParticles.EndFrame();
    m_fluid.EndFrame();
    const bgfx::Stats* stats = bgfx::getStats();
    m_frameStats = {};
    if (stats == nullptr) {
        return;
    }
    m_frameStats.drawCalls = stats->numDraw;
    m_frameStats.computeCalls = stats->numCompute;
    m_frameStats.gpuFrame = stats->gpuFrameNum;
    m_frameStats.textureCount = stats->numTextures;
    m_frameStats.textureMemoryBytes = stats->textureMemoryUsed > 0
        ? static_cast<std::uint64_t>(stats->textureMemoryUsed) : 0;
    m_frameStats.renderTargetMemoryBytes = stats->rtMemoryUsed > 0
        ? static_cast<std::uint64_t>(stats->rtMemoryUsed) : 0;
    m_frameStats.transientVertexBytes = stats->transientVbUsed > 0
        ? static_cast<std::uint32_t>(stats->transientVbUsed) : 0;
    m_frameStats.transientIndexBytes = stats->transientIbUsed > 0
        ? static_cast<std::uint32_t>(stats->transientIbUsed) : 0;
    if (stats->gpuTimerFreq > 0 && stats->gpuTimeEnd >= stats->gpuTimeBegin) {
        m_frameStats.gpuFrameMs = 1000.0f
            * static_cast<float>(stats->gpuTimeEnd - stats->gpuTimeBegin)
            / static_cast<float>(stats->gpuTimerFreq);
        m_frameStats.valid = stats->gpuTimeEnd > stats->gpuTimeBegin;
    }
    m_frameStats.passCount = stats->numViews;
    if (stats->gpuTimerFreq > 0 && stats->viewStats != nullptr) {
        for (std::uint16_t index = 0; index < stats->numViews; ++index) {
            const bgfx::ViewStats& view = stats->viewStats[index];
            if (view.gpuTimeEnd < view.gpuTimeBegin) {
                continue;
            }
            const float passMs = 1000.0f
                * static_cast<float>(view.gpuTimeEnd - view.gpuTimeBegin)
                / static_cast<float>(stats->gpuTimerFreq);
            if (passMs <= m_frameStats.slowestPassMs) {
                continue;
            }
            m_frameStats.slowestPassMs = passMs;
            m_frameStats.slowestPass.fill('\0');
            if (view.name != nullptr) {
                std::strncpy(m_frameStats.slowestPass.data(), view.name,
                             m_frameStats.slowestPass.size() - 1);
            }
        }
    }
}

std::uint32_t BgfxRenderBackend::NativeWindowRetirementFrames() const noexcept
{
    return kNativeWindowRetirementFrames;
}

void BgfxRenderBackend::Shutdown() noexcept
{
    if (m_bgfxInitialized) {
        m_meshProgram.Shutdown();
        for (auto& [view, slot] : m_views) {
            DestroyForwardPlusContexts(slot);
        }
        m_uniforms.Shutdown();
        m_postProcess.Shutdown();
        m_bloom.Shutdown();
        m_smaa.Shutdown();
        m_skyRenderer.Shutdown();
        m_volumeCloud.Shutdown();
        m_volumeSmoke.Shutdown();
        m_gpuLightCuller.Shutdown();
        m_gpuParticles.Shutdown();
        m_water.Shutdown();
        m_fluid.Shutdown();
        m_shadowMap.Shutdown();
        m_debugText.Shutdown();
        m_uiRenderer.Shutdown();
        m_textureCache.Clear();
        m_pendingDraws.clear();
        m_pendingShadowCasters.clear();
        m_pendingParticleEmitters.clear();
        m_meshes.Clear();
        for (auto& [view, slot] : m_views) {
            if (slot.shadowReady) {
                for (std::uint32_t cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
                    m_shadowMap.DestroyViewTarget(slot.shadowFbs[cascade], slot.shadowTextures[cascade]);
                }
                slot.shadowReady = false;
            }
            DestroyViewTargets(slot);
        }
        m_views.clear();
        m_viewBlocks.Reset();

        bgfx::shutdown();
        m_bgfxInitialized = false;
    }
    Particles::ParticleSimulationRuntime::SetGpuAvailability(
        Particles::GpuParticleAvailability::Unknown);
    m_deviceWindow.Close();
    m_initialized = false;
    m_prepared = false;
    m_activeMsaa = MsaaLevel::Off;
    m_activeVsync = false;
    m_frameStats = {};
}

} // namespace Concord
