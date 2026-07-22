#include "engine/render/backend/BgfxRenderBackend.h"

#include "engine/debug/Logger.h"
#include "engine/render/frame/SkyEnvironment.h"
#include "engine/render/volume/BgfxVolumeCloudRenderer.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <unordered_map>

namespace Concord {

bool BgfxRenderBackend::CreateFramebufferForView(RenderViewHandle view, const RenderViewInit& init)
{
    const bgfx::FrameBufferHandle framebuffer = bgfx::createFrameBuffer(
        init.nativeWindowHandle,
        static_cast<std::uint16_t>(init.width),
        static_cast<std::uint16_t>(init.height),
        bgfx::TextureFormat::RGBA8,
        bgfx::TextureFormat::D24S8);
    if (!bgfx::isValid(framebuffer)) {
        Debug::Logger::Error("Render", "createFrameBuffer failed for a window");
        return false;
    }

    // Preserve the post-process bgfx view ids assigned once in CreateView across
    // rebuilds (resize / MSAA change), so a view keeps its stable view ids.
    const auto existing = m_views.find(view);

    ViewSlot slot;
    slot.framebuffer = framebuffer;
    slot.window = init.window;
    slot.nativeWindowHandle = init.nativeWindowHandle;
    slot.width = init.width;
    slot.height = init.height;
    slot.aa = init.aa;
    if (existing != m_views.end()) {
        if (slot.window == kInvalidWindowId) {
            slot.window = existing->second.window;
        }
        slot.presentView = existing->second.presentView;
        slot.smaaEdgeView = existing->second.smaaEdgeView;
        slot.smaaWeightView = existing->second.smaaWeightView;
        slot.smaaBlendView = existing->second.smaaBlendView;
        slot.bloomViews = existing->second.bloomViews;
        slot.shadowViews = existing->second.shadowViews;
        slot.shadowFbs = existing->second.shadowFbs;
        slot.shadowTextures = existing->second.shadowTextures;
        slot.shadowReady = existing->second.shadowReady;
        slot.planarView = existing->second.planarView;
        slot.planarForwardPlus = existing->second.planarForwardPlus;
        slot.reflectionViews = existing->second.reflectionViews;
        slot.reflectionForwardPlus = existing->second.reflectionForwardPlus;
        slot.particleComputeView = existing->second.particleComputeView;
        slot.depthPrepassView = existing->second.depthPrepassView;
        slot.lightCullView = existing->second.lightCullView;
        slot.mainForwardPlus = existing->second.mainForwardPlus;
        slot.cloudView = existing->second.cloudView;
        slot.cloudCompositeView = existing->second.cloudCompositeView;
        slot.smokeView = existing->second.smokeView;
        slot.smokeCompositeView = existing->second.smokeCompositeView;
        // Planar, reflection, cloud and smoke targets are retired and recreated as needed.
    }
    const RenderViewHandle presentView = slot.presentView;
    const RenderViewHandle planarView = slot.planarView;

    const std::uint16_t w = static_cast<std::uint16_t>(init.width);
    const std::uint16_t h = static_cast<std::uint16_t>(init.height);

    // Always render the scene into an offscreen RGBA16F target, then blit to the
    // window through presentView with final 8-bit dither. Direct-to-swapchain
    // (especially with MSAA resolve) quantises early and bakes isophote banding
    // that no mesh-shader dither can fix. Fall back only if HDR RT setup fails.
    const bool smaa = init.aa == AntiAliasing::Smaa2 || init.aa == AntiAliasing::Smaa4;
    if (presentView != kInvalidRenderView) {
        // SMAA is optional here: RunPostProcess falls back to FXAA, but both
        // paths need the same HDR scene target for effects and final present.
        if (smaa && !m_smaa.EnsureReady()) {
            Debug::Logger::Warn(
                "Render", "SMAA setup failed for aa=%s; post-process will fall back to FXAA",
                ToString(init.aa));
        }
        if (m_postProcess.EnsureReady()
            && m_postProcess.CreateTargets(init.width, init.height, slot.scene)) {
            bgfx::setViewFrameBuffer(view, slot.scene.framebuffer);
            bgfx::setViewClear(view, BGFX_CLEAR_COLOR, SkyBackgroundRgba(), 1.0f, 0);
            bgfx::setViewRect(view, 0, 0, w, h);
            bgfx::setViewFrameBuffer(slot.depthPrepassView, slot.scene.framebuffer);
            bgfx::setViewClear(slot.depthPrepassView, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
            bgfx::setViewRect(slot.depthPrepassView, 0, 0, w, h);

            bgfx::setViewFrameBuffer(presentView, framebuffer);
            bgfx::setViewClear(presentView, BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
            bgfx::setViewRect(presentView, 0, 0, w, h);

            // HDR bloom scratch targets (half-res). Optional: if setup fails the
            // present pass simply composites nothing and the scene still shows.
            if (m_bloom.EnsureReady()) {
                m_bloom.CreateTargets(init.width, init.height, slot.bloom);
            }

            // Half-resolution RGBA16F target for the volumetric cloud march
            // (UE-style downsampled raymarch); the composite pass upsamples it
            // into the scene color. Bilinear + clamp so the upscale is smooth.
            // Optional: if it fails the cloud pass simply skips.
            const std::uint32_t divisor = BgfxVolumeCloudRenderer::kResolutionDivisor;
            slot.cloudWidth = std::max(init.width / divisor, 1u);
            slot.cloudHeight = std::max(init.height / divisor, 1u);
            slot.cloudColor = bgfx::createTexture2D(
                static_cast<std::uint16_t>(slot.cloudWidth),
                static_cast<std::uint16_t>(slot.cloudHeight), false, 1,
                bgfx::TextureFormat::RGBA16F,
                BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
            if (bgfx::isValid(slot.cloudColor)) {
                slot.cloudFb = bgfx::createFrameBuffer(1, &slot.cloudColor, true);
            }

            // Reduced-resolution RGBA16F target for the volumetric smoke march;
            // the composite pass upsamples it into the scene color. A second
            // attachment (R32F device depth, point-sampled) lets the composite
            // pass weight its 4-tap upsample by depth similarity to the
            // full-res surface, avoiding the blur/halo a naive bilinear
            // upsample causes at foreground silhouettes. Optional: if it fails
            // the smoke pass simply skips.
            const std::uint32_t smokeDivisor = BgfxSmokeRenderer::kResolutionDivisor;
            slot.smokeWidth = std::max(init.width / smokeDivisor, 1u);
            slot.smokeHeight = std::max(init.height / smokeDivisor, 1u);
            slot.smokeColor = bgfx::createTexture2D(
                static_cast<std::uint16_t>(slot.smokeWidth),
                static_cast<std::uint16_t>(slot.smokeHeight), false, 1,
                bgfx::TextureFormat::RGBA16F,
                BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
            slot.smokeDepthProxy = bgfx::createTexture2D(
                static_cast<std::uint16_t>(slot.smokeWidth),
                static_cast<std::uint16_t>(slot.smokeHeight), false, 1,
                bgfx::TextureFormat::R32F,
                BGFX_TEXTURE_RT | BGFX_SAMPLER_POINT
                    | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
            if (bgfx::isValid(slot.smokeColor) && bgfx::isValid(slot.smokeDepthProxy)) {
                bgfx::TextureHandle smokeAttachments[2] = {slot.smokeColor, slot.smokeDepthProxy};
                slot.smokeFb = bgfx::createFrameBuffer(2, smokeAttachments, true);
            }

            m_views[view] = slot;
            return true;
        }
        Debug::Logger::Warn("Render",
                            "HDR offscreen/present setup failed for aa=%s; falling back to direct 8-bit",
                            ToString(init.aa));
    }

    // Fallback: render straight into the window's swap-chain framebuffer.
    bgfx::setViewFrameBuffer(view, framebuffer);
    bgfx::setViewClear(view, BGFX_CLEAR_COLOR, SkyBackgroundRgba(), 1.0f, 0);
    bgfx::setViewRect(view, 0, 0, w, h);
    bgfx::setViewFrameBuffer(slot.depthPrepassView, framebuffer);
    bgfx::setViewClear(slot.depthPrepassView, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
    bgfx::setViewRect(slot.depthPrepassView, 0, 0, w, h);
    m_views[view] = slot;
    return true;
}

RenderViewHandle BgfxRenderBackend::CreateView(const RenderViewInit& init)
{
    if (!m_initialized
        || init.nativeWindowHandle == nullptr
        || init.width == 0
        || init.height == 0
        || init.width > std::numeric_limits<std::uint16_t>::max()
        || init.height > std::numeric_limits<std::uint16_t>::max()) {
        Debug::Logger::Error("Render", "invalid render view initialization data");
        return kInvalidRenderView;
    }

    // Allocate a fixed pass block for every window so runtime AA changes never
    // need new view ids. Fail cleanly before reaching bgfx's configured limit.
    const std::uint32_t maxViews = bgfx::getCaps()->limits.maxViews;
    const std::uint32_t firstView = m_viewBlocks.Acquire();
    if (firstView == kInvalidRenderView) {
        Debug::Logger::Error("Render", "render view id capacity exhausted (%u/%u)",
                             static_cast<unsigned>(m_views.size() * kViewsPerWindow), maxViews);
        return kInvalidRenderView;
    }
    std::uint32_t nextView = firstView;

    // Shadow depth pass runs in its own bgfx view before the scene view (lower
    // id => rendered first), so the shadow map is ready for the scene pass to
    // sample. Allocated unconditionally: a window with no shadow-casting light
    // still benefits from a stable view slot, and ensures the scene pass always
    // has shadow inputs bound (disabled or not) rather than left unset.
    std::array<RenderViewHandle, kShadowCascadeCount> shadowViews{};
    for (RenderViewHandle& shadowView : shadowViews) {
        shadowView = static_cast<RenderViewHandle>(nextView++);
    }
    // Particle simulation must precede every view that reads its state buffer.
    const RenderViewHandle particleComputeViewId = static_cast<RenderViewHandle>(nextView++);
    // planar (mirrored scene) → six-face reflection capture → scene
    const RenderViewHandle planarViewId = static_cast<RenderViewHandle>(nextView++);
    std::array<RenderViewHandle, ReflectionCapture::kFaceCount> reflectionViews{};
    for (RenderViewHandle& reflectionView : reflectionViews) {
        reflectionView = static_cast<RenderViewHandle>(nextView++);
    }
    const RenderViewHandle depthPrepassViewId = static_cast<RenderViewHandle>(nextView++);
    // Forward+ GPU light-cull compute dispatch runs right before the scene view
    // so its cluster range/index textures are ready when the scene samples them.
    const RenderViewHandle lightCullViewId = static_cast<RenderViewHandle>(nextView++);
    const RenderViewHandle view = static_cast<RenderViewHandle>(nextView++);
    // Volumetric clouds run right after the scene view and before the
    // SMAA/bloom/present block: a half-res march view then a composite view
    // that upsamples into the HDR scene color.
    const RenderViewHandle cloudViewId = static_cast<RenderViewHandle>(nextView++);
    const RenderViewHandle cloudCompositeViewId = static_cast<RenderViewHandle>(nextView++);
    // Local volumetric smoke: a reduced-res march view then a composite view,
    // right after the clouds and before the SMAA/bloom/present block.
    const RenderViewHandle smokeViewId = static_cast<RenderViewHandle>(nextView++);
    const RenderViewHandle smokeCompositeViewId = static_cast<RenderViewHandle>(nextView++);

    ViewSlot seed;
    seed.shadowViews = shadowViews;
    seed.planarView = planarViewId;
    seed.reflectionViews = reflectionViews;
    seed.particleComputeView = particleComputeViewId;
    seed.depthPrepassView = depthPrepassViewId;
    seed.lightCullView = lightCullViewId;
    seed.cloudView = cloudViewId;
    seed.cloudCompositeView = cloudCompositeViewId;
    seed.smokeView = smokeViewId;
    seed.smokeCompositeView = smokeCompositeViewId;
    m_uniforms.EnsureReady();
    bool forwardPlusReady = m_uniforms.ForwardPlusReady();
    if (forwardPlusReady) {
        forwardPlusReady = m_uniforms.CreateForwardPlusContext(seed.mainForwardPlus);
        forwardPlusReady = m_uniforms.CreateForwardPlusContext(seed.planarForwardPlus)
            && forwardPlusReady;
        for (BgfxSceneUniforms::ForwardPlusContext& context : seed.reflectionForwardPlus) {
            forwardPlusReady = m_uniforms.CreateForwardPlusContext(context) && forwardPlusReady;
        }
    }
    if (!forwardPlusReady) {
        Debug::Logger::Warn(
            "Render", "Forward+ resources unavailable; affected passes use fixed lights");
    }
    if (m_shadowMap.EnsureReady()) {
        seed.shadowReady = true;
        for (std::uint32_t cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
            if (!m_shadowMap.CreateViewTarget(m_shadowConfig.resolution,
                                              seed.shadowFbs[cascade],
                                              seed.shadowTextures[cascade])) {
                seed.shadowReady = false;
                break;
            }
        }
        if (!seed.shadowReady) {
            for (std::uint32_t cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
                m_shadowMap.DestroyViewTarget(seed.shadowFbs[cascade], seed.shadowTextures[cascade]);
            }
        }
    }

    // Always allocate a present view: final dithered blit to the 8-bit swap
    // chain (anti-banding). SMAA still needs its three intermediate views.
    // Ordered after the scene view (higher id => rendered later).
    {
        seed.smaaEdgeView = static_cast<RenderViewHandle>(nextView++);
        seed.smaaWeightView = static_cast<RenderViewHandle>(nextView++);
        seed.smaaBlendView = static_cast<RenderViewHandle>(nextView++);
        // Bloom mip-chain passes run after the scene (and SMAA) views but
        // before present, so the present composite can add the finished glow.
        for (std::uint32_t i = 0; i < BgfxBloom::kMaxViews; ++i) {
            seed.bloomViews[i] = static_cast<RenderViewHandle>(nextView++);
        }
        seed.presentView = static_cast<RenderViewHandle>(nextView++);
    }
    const std::uint32_t allocatedViews = nextView - firstView;
    if (allocatedViews != kViewsPerWindow) {
        Debug::Logger::Error("Render", "view block layout mismatch: allocated %u, expected %u",
                             allocatedViews, kViewsPerWindow);
        if (seed.shadowReady) {
            for (std::uint32_t cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
                m_shadowMap.DestroyViewTarget(seed.shadowFbs[cascade],
                                              seed.shadowTextures[cascade]);
            }
        }
        DestroyForwardPlusContexts(seed);
        m_viewBlocks.Release(firstView);
        return kInvalidRenderView;
    }
    m_views[view] = seed;

    char name[64];
    for (std::uint32_t cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
        std::snprintf(name, sizeof(name), "View%u/Shadow%u", view, cascade);
        bgfx::setViewName(seed.shadowViews[cascade], name);
    }
    bgfx::setViewName(seed.planarView, "PlanarReflection");
    bgfx::setViewName(seed.particleComputeView, "GPU particles: simulate");
    bgfx::setViewName(seed.depthPrepassView, "ForwardPlus/DepthPrepass");
    bgfx::setViewName(seed.lightCullView, "ForwardPlus/LightCull");
    static constexpr const char* kReflectionNames[ReflectionCapture::kFaceCount] = {
        "Reflection/+X", "Reflection/-X", "Reflection/+Y",
        "Reflection/-Y", "Reflection/+Z", "Reflection/-Z",
    };
    for (std::uint32_t face = 0; face < ReflectionCapture::kFaceCount; ++face) {
        bgfx::setViewName(seed.reflectionViews[face], kReflectionNames[face]);
    }
    std::snprintf(name, sizeof(name), "View%u/Scene", view);
    bgfx::setViewName(view, name);
    bgfx::setViewName(seed.cloudView, "VolumetricCloud/March");
    bgfx::setViewName(seed.cloudCompositeView, "VolumetricCloud/Composite");
    bgfx::setViewName(seed.smokeView, "VolumetricSmoke/March");
    bgfx::setViewName(seed.smokeCompositeView, "VolumetricSmoke/Composite");
    bgfx::setViewName(seed.smaaEdgeView, "SMAA/Edges");
    bgfx::setViewName(seed.smaaWeightView, "SMAA/Weights");
    bgfx::setViewName(seed.smaaBlendView, "SMAA/Blend");
    for (std::uint32_t bloom = 0; bloom < BgfxBloom::kMaxViews; ++bloom) {
        std::snprintf(name, sizeof(name), "Bloom/%u", bloom);
        bgfx::setViewName(seed.bloomViews[bloom], name);
    }
    bgfx::setViewName(seed.presentView, "Present");

    if (!CreateFramebufferForView(view, init)) {
        DestroyViewTargets(m_views[view]);
        DestroyForwardPlusContexts(m_views[view]);
        if (m_views[view].shadowReady) {
            for (std::uint32_t cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
                m_shadowMap.DestroyViewTarget(m_views[view].shadowFbs[cascade],
                                              m_views[view].shadowTextures[cascade]);
            }
        }
        m_views.erase(view);
        m_viewBlocks.Release(firstView);
        return kInvalidRenderView;
    }
    if (!EnsureResetState(init.msaa, init.vsync)) {
        DestroyView(view);
        return kInvalidRenderView;
    }
    Debug::Logger::Debug("Render", "created view %u (%ux%u), aa=%s", view, init.width, init.height,
                         ToString(init.aa));
    return view;
}

bool BgfxRenderBackend::RecreateView(RenderViewHandle view, const RenderViewInit& init)
{
    if (!m_initialized
        || init.nativeWindowHandle == nullptr
        || init.width == 0
        || init.height == 0
        || init.width > std::numeric_limits<std::uint16_t>::max()
        || init.height > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }

    const auto it = m_views.find(view);
    if (it == m_views.end()) {
        return false;
    }

    const MsaaLevel previousMsaa = m_activeMsaa;
    const bool resetSucceeded = EnsureResetState(init.msaa, init.vsync, view, &init);
    if (previousMsaa != init.msaa) {
        return resetSucceeded;
    }

    const ViewSlot oldTargets = it->second;
    if (!CreateFramebufferForView(view, init)) {
        return false;
    }
    ViewSlot retired = oldTargets;
    DestroyViewTargets(retired);
    return true;
}

void BgfxRenderBackend::DestroyView(RenderViewHandle view)
{
    const auto it = m_views.find(view);
    if (it == m_views.end()) {
        return;
    }
    if (it->second.shadowReady) {
        for (std::uint32_t cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
            m_shadowMap.DestroyViewTarget(it->second.shadowFbs[cascade],
                                          it->second.shadowTextures[cascade]);
        }
        it->second.shadowReady = false;
    }
    DestroyViewTargets(it->second);
    DestroyForwardPlusContexts(it->second);
    m_gpuParticles.DestroyView(view);
    m_smaa.Release(view);
    m_viewBlocks.Release(it->second.shadowViews[0]);
    m_views.erase(it);
    m_pendingDraws.erase(view);
    m_pendingParticleEmitters.erase(view);
}

void BgfxRenderBackend::DestroyViewTargets(ViewSlot& slot)
{
    // The half-res cloud framebuffer owns its color texture (destroyTextures
    // was true at creation), so destroying it frees cloudColor too.
    if (bgfx::isValid(slot.cloudFb)) {
        bgfx::destroy(slot.cloudFb);
        slot.cloudFb = BGFX_INVALID_HANDLE;
    }
    slot.cloudColor = BGFX_INVALID_HANDLE;
    // The smoke framebuffer owns both attachments (destroyTextures=true).
    if (bgfx::isValid(slot.smokeFb)) {
        bgfx::destroy(slot.smokeFb);
        slot.smokeFb = BGFX_INVALID_HANDLE;
    }
    slot.smokeColor = BGFX_INVALID_HANDLE;
    slot.smokeDepthProxy = BGFX_INVALID_HANDLE;
    m_postProcess.DestroyTargets(slot.scene);
    m_bloom.DestroyTargets(slot.bloom);
    if (bgfx::isValid(slot.framebuffer)) {
        bgfx::destroy(slot.framebuffer);
        slot.framebuffer = BGFX_INVALID_HANDLE;
    }
    m_planarReflection.DestroyTargets(slot.planar);
    slot.planarValid = false;
    m_reflectionCapture.DestroyTargets(slot.reflection);
    slot.reflectionSignatureValid = false;
    slot.reflectionValid = false;
    slot.reflectionBoxValid = false;
    slot.reflectionInitialized = false;
    slot.reflectionFaceCursor = 0;
    slot.reflectionPendingFaces = 0;
    // Pass view ids and Forward+ contexts remain stable across target rebuilds.
}

void BgfxRenderBackend::DestroyForwardPlusContexts(ViewSlot& slot)
{
    m_uniforms.DestroyForwardPlusContext(slot.mainForwardPlus);
    m_uniforms.DestroyForwardPlusContext(slot.planarForwardPlus);
    for (BgfxSceneUniforms::ForwardPlusContext& context : slot.reflectionForwardPlus) {
        m_uniforms.DestroyForwardPlusContext(context);
    }
}

} // namespace Concord
