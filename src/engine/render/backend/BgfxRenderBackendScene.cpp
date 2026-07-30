#include "engine/render/backend/BgfxRenderBackend.h"

#include "engine/debug/DebugOverlay.h"
#include "engine/debug/Logger.h"
#include "engine/render/backend/BgfxMathConverters.h"
#include "engine/render/frame/SkyEnvironment.h"
#include "engine/ui/UiSurface.h"
#include "engine/utils/PrintString.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace {

namespace Detail = Concord::RenderDetail;
using Concord::CullMode;
using Detail::ToBgfxDepthTest;
using Detail::ToBgfxCull;
using Detail::ToBgfxBlend;

/** Reverses face culling for a mirrored camera, whose transform flips winding. */
CullMode MirroredCull(CullMode mode) noexcept
{
    switch (mode) {
        case CullMode::Back:  return CullMode::Front;
        case CullMode::Front: return CullMode::Back;
        case CullMode::None:  return CullMode::None;
    }
    return CullMode::None;
}

bool ParticipatesInDepthPrepass(const Concord::RenderMaterial& material,
                                Concord::RenderEffect effect) noexcept
{
    return effect != Concord::RenderEffect::ParticleBillboard
        && material.blend == Concord::Material::BlendMode::Opaque
        && material.depthWrite
        && material.depthTest == Concord::DepthTest::LessEqual;
}

/**
 * Projects the directional sun to normalized viewport coordinates (y-up, the
 * same space the screen effects author in) for the lens-flare pass.
 * @return true when the sun is in front of the camera and roughly on screen.
 */
bool ComputeSunScreenPos(const float viewMtx[16], const float projMtx[16],
                         const Concord::RenderLight* lights, std::uint32_t lightCount,
                         const float eye[3], float& outU, float& outV) noexcept
{
    if (lights == nullptr) {
        return false;
    }
    const Concord::RenderLight* sun = nullptr;
    const std::uint32_t count = std::min(lightCount, Concord::kMaxRenderLights);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (lights[i].type == Concord::LightType::Directional) {
            sun = &lights[i];
            if (lights[i].sun) {
                break;
            }
        }
    }
    if (sun == nullptr) {
        return false;
    }
    // A point far along the direction toward the sun (light travels along
    // `direction`, so the sun sits along -direction).
    const float toSun[3] = {-sun->direction[0], -sun->direction[1], -sun->direction[2]};
    const float far = 1.0e6f;
    const float wx = eye[0] + toSun[0] * far;
    const float wy = eye[1] + toSun[1] * far;
    const float wz = eye[2] + toSun[2] * far;
    float vp[16];
    bx::mtxMul(vp, viewMtx, projMtx);
    // Column-major clip = viewProj * world (matches the sky's invViewProj use).
    const float cx = vp[0] * wx + vp[4] * wy + vp[8] * wz + vp[12];
    const float cy = vp[1] * wx + vp[5] * wy + vp[9] * wz + vp[13];
    const float cw = vp[3] * wx + vp[7] * wy + vp[11] * wz + vp[15];
    if (cw <= 1e-4f) {
        return false;
    }
    const float ndcX = cx / cw;
    const float ndcY = cy / cw;
    outU = ndcX * 0.5f + 0.5f;
    outV = ndcY * 0.5f + 0.5f;
    return ndcX > -1.3f && ndcX < 1.3f && ndcY > -1.3f && ndcY < 1.3f;
}

/** Fallback camera used when a frame does not contain a camera view. */
constexpr bx::Vec3 kCameraEye{0.0f, 5.0f, -10.0f};
constexpr bx::Vec3 kCameraAt{0.0f, 0.0f, 0.0f};
constexpr bx::Vec3 kCameraUp{0.0f, 1.0f, 0.0f};
constexpr float kCameraFovDeg = 60.0f;
constexpr float kCameraNear = 0.1f;
constexpr float kCameraFar = 100.0f;

// Keep the bloom threshold above the tonemapped [0, 1] range so the mip chain
// primarily receives HDR emissive content. Intensity controls the composite
// weight and filter radius controls the tent-filter spread.
constexpr bool kBloomEnabled = true;
// The shader's 20% soft knee begins at 0.8 * threshold. A threshold of 1.25
// therefore starts exactly above the tonemapped scene's [0,1] ceiling, keeping
// ordinary white surfaces out while preserving genuinely HDR particles.
constexpr float kBloomThreshold = 1.25f;
constexpr float kBloomIntensity = 0.16f;
constexpr float kBloomFilterRadius = 1.0f;

} // namespace

namespace Concord {

void BgfxRenderBackend::MaybeCaptureFrame(const ViewSlot& slot)
{
    // Opt-in frame grab for looking at what the renderer actually produced
    // without a person at the window: CONCORD_CAPTURE_FRAME=<n> writes the n-th
    // rendered frame of a window to CONCORD_CAPTURE_PATH (default capture.tga).
    static const long captureFrame = [] {
        const char* value = std::getenv("CONCORD_CAPTURE_FRAME");
        return value != nullptr ? std::strtol(value, nullptr, 10) : 0L;
    }();
    if (captureFrame <= 0 || !bgfx::isValid(slot.framebuffer)) {
        return;
    }
    static long frameIndex = 0;
    if (++frameIndex != captureFrame) {
        return;
    }
    const char* path = std::getenv("CONCORD_CAPTURE_PATH");
    bgfx::requestScreenShot(slot.framebuffer, path != nullptr ? path : "capture.tga");
}

void BgfxRenderBackend::RunPostProcess(RenderViewHandle view, const ViewSlot& slot,
                                       bool bloomSource, const ViewEffectState* effects)
{
    // Build the bloom contribution from the HDR scene before any AA pass runs;
    // the present composite adds it on top of the resolved image. Invalid /
    // intensity 0 when bloom setup failed, so the present stays a plain blit.
    bgfx::TextureHandle bloomTex = BGFX_INVALID_HANDLE;
    float bloomIntensity = 0.0f;
    if (kBloomEnabled && bloomSource && slot.bloom.Valid()) {
        bloomTex = m_bloom.Generate(slot.bloomViews.data(), BgfxBloom::kMaxViews,
                                    slot.scene.color, slot.width, slot.height, slot.bloom,
                                    kBloomThreshold, kBloomFilterRadius);
        if (bgfx::isValid(bloomTex)) {
            bloomIntensity = kBloomIntensity;
        }
    }

    const bool smaa = slot.aa == AntiAliasing::Smaa2 || slot.aa == AntiAliasing::Smaa4;
    if (smaa) {
        // SMAA runs its three passes into its own targets and returns the AA'd
        // texture; the present pass then blits it to the window with dither.
        const bgfx::TextureHandle result = m_smaa.Run(view, slot.smaaEdgeView, slot.smaaWeightView,
                                                      slot.smaaBlendView, slot.scene.color, slot.width, slot.height,
                                                      slot.aa);
        if (bgfx::isValid(result)) {
            m_postProcess.Blit(slot.presentView, result, slot.width, slot.height,
                               bloomTex, bloomIntensity, effects);
            return;
        }
        // SMAA unavailable this frame: fall back to FXAA so the window still shows.
        m_postProcess.Present(slot.presentView, slot.scene.color, slot.width, slot.height,
                              AntiAliasing::Fxaa, bloomTex, bloomIntensity, effects);
        return;
    }
    if (slot.aa == AntiAliasing::Fxaa) {
        m_postProcess.Present(slot.presentView, slot.scene.color, slot.width, slot.height,
                              AntiAliasing::Fxaa, bloomTex, bloomIntensity, effects);
        return;
    }
    // Off / MSAA*: HDR offscreen �?dithered blit (no FXAA blur). Hardware MSAA
    // on the swap chain is not used when the HDR present path is active; the
    // anti-banding present is more important for flat-lit surfaces.
    m_postProcess.Blit(slot.presentView, slot.scene.color, slot.width, slot.height,
                       bloomTex, bloomIntensity, effects);
}

MeshHandle BgfxRenderBackend::CreateMesh(const MeshData& data)
{
    if (!m_initialized) {
        return MeshHandle::Invalid();
    }
    return m_meshes.Create(data);
}

void BgfxRenderBackend::DestroyMesh(MeshHandle mesh)
{
    m_meshes.Destroy(mesh);
}

void BgfxRenderBackend::SubmitMesh(RenderViewHandle view, const MeshDrawCommand& command)
{
    if (!m_initialized || view == kInvalidRenderView) {
        return;
    }
    if (command.effect == RenderEffect::ParticleBillboard) {
        m_meshProgram.EnsureParticleReady();
    } else {
        m_meshProgram.EnsureReady();
    }
    m_pendingDraws[view].push_back(command);
}

void BgfxRenderBackend::SubmitShadowCaster(RenderViewHandle view,
                                           const MeshDrawCommand& command)
{
    if (!m_initialized || view == kInvalidRenderView
        || command.material.blend != Material::BlendMode::Opaque
        || command.effect == RenderEffect::ParticleBillboard) {
        return;
    }
    // Depth-only path: no material program is needed, but the skinned shadow
    // program still has to be ready before the pass submits.
    m_pendingShadowCasters[view].push_back(command);
}

void BgfxRenderBackend::SubmitParticleEmitter(
    RenderViewHandle view, const RenderParticleEmitter& emitter)
{
    if (!m_initialized || view == kInvalidRenderView || emitter.emitterKey == 0) {
        return;
    }
    m_pendingParticleEmitters[view].push_back(emitter);
}

void BgfxRenderBackend::RenderView(RenderViewHandle view, const CameraView* camera,
                                   const RenderLight* lights, std::uint32_t lightCount,
                                   const SkyEnvironment* sky,
                                   const RenderSmokeVolume* smokeVolumes,
                                   std::uint32_t smokeVolumeCount)
{
    const auto it = m_views.find(view);
    if (it == m_views.end()) {
        return;
    }
    ViewSlot& slot = it->second;
    bgfx::setViewRect(view, 0, 0, static_cast<std::uint16_t>(slot.width), static_cast<std::uint16_t>(slot.height));

    const auto pendingIt = m_pendingDraws.find(view);
    const bool hasDraws = pendingIt != m_pendingDraws.end()
        && !pendingIt->second.empty();
    const auto particleIt = m_pendingParticleEmitters.find(view);
    const bool hasGpuParticles = particleIt != m_pendingParticleEmitters.end()
        && !particleIt->second.empty();
    // Offscreen HDR scene + present blit (dither to 8-bit). Used for every AA
    // mode when the RT came up; not limited to FXAA/SMAA.
    const bool postProcess = slot.scene.Valid() && slot.presentView != kInvalidRenderView;

    const float aspect = static_cast<float>(slot.width) / static_cast<float>(slot.height);

    // Resolve the view transform and projection parameters from the active
    // camera, or fall back to the engine's default framing when none is set.
    // Computed BEFORE the shadow pass so the shadow frustum can be tightened
    // to the camera frustum (the single biggest quality lever for shadow
    // sharpness �?without this the shadow map would waste texels covering the
    // 40×40 ground plane the camera can't even see all of).
    float viewMtx[16];
    Projection projection = Projection::Perspective;
    float fovYDegrees = kCameraFovDeg;
    float orthoHeight = 10.0f;
    float nearPlane = kCameraNear;
    float farPlane = kCameraFar;
    if (camera != nullptr) {
        std::memcpy(viewMtx, camera->viewMatrix, sizeof(viewMtx));
        projection = camera->projection;
        fovYDegrees = camera->fovYDegrees;
        orthoHeight = camera->orthoHeight;
        nearPlane = camera->nearPlane;
        farPlane = camera->farPlane;
    } else {
        bx::mtxLookAt(viewMtx, kCameraEye, kCameraAt, kCameraUp);
    }

    float cameraEye[3] = {kCameraEye.x, kCameraEye.y, kCameraEye.z};
    if (camera != nullptr) {
        std::copy_n(camera->eye, 3, cameraEye);
    }

    float projMtx[16];
    const bool homogeneousDepth = bgfx::getCaps()->homogeneousDepth;
    if (projection == Projection::Orthographic) {
        const float halfHeight = orthoHeight * 0.5f;
        const float halfWidth = halfHeight * aspect;
        bx::mtxOrtho(projMtx, -halfWidth, halfWidth, -halfHeight, halfHeight, nearPlane, farPlane, 0.0f, homogeneousDepth);
    } else {
        bx::mtxProj(projMtx, fovYDegrees, aspect, nearPlane, farPlane, homogeneousDepth);
    }

    // Per-frame view-effect state; add the resolved sun screen position so the
    // present pass can draw the lens flare when the camera enabled it.
    ViewEffectState flareState{};
    const bool hasEffects = camera != nullptr;
    if (hasEffects) {
        flareState = camera->effects;
        if (flareState.lensFlareEnabled != 0) {
            float sunU = 0.5f;
            float sunV = 0.5f;
            const bool visible = ComputeSunScreenPos(viewMtx, projMtx, lights, lightCount,
                                                     cameraEye, sunU, sunV);
            flareState.lensFlareSunPos[0] = sunU;
            flareState.lensFlareSunPos[1] = sunV;
            flareState.lensFlareSunVisible = visible ? 1u : 0u;
        }
    }
    const ViewEffectState* effectsPtr = hasEffects ? &flareState : nullptr;

    const SkyEnvironment& environment = sky != nullptr ? *sky : kDefaultSkyEnvironment;
    bgfx::setViewClear(view, BGFX_CLEAR_COLOR,
                       SkyBackgroundRgba(environment), 1.0f, 0);
    bgfx::setViewTransform(view, viewMtx, projMtx);
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
    bgfx::touch(view);
    // The main scene view suppresses the inline sky cloud march: the
    // independent volumetric cloud pass (below) owns clouds here, compositing
    // them against scene depth instead of painting the far background.
    m_skyRenderer.Draw(view, viewMtx, projMtx, cameraEye, environment,
                       lights, lightCount, false, /*drawClouds=*/false);

    if (slot.depthPrepassView != kInvalidRenderView) {
        bgfx::setViewClear(slot.depthPrepassView, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
        bgfx::setViewTransform(slot.depthPrepassView, viewMtx, projMtx);
        bgfx::setViewMode(slot.depthPrepassView, bgfx::ViewMode::Sequential);
        bgfx::touch(slot.depthPrepassView);
    }

    if (hasGpuParticles) {
        m_gpuParticles.Simulate(
            view, slot.particleComputeView, particleIt->second);
    }
    if (!hasDraws) {
        if (pendingIt != m_pendingDraws.end()) {
            pendingIt->second.clear();
        }
        // Nothing visible receives a shadow this frame, so the casters queued
        // for it are dropped rather than carried into the next frame.
        if (const auto casterIt = m_pendingShadowCasters.find(view);
            casterIt != m_pendingShadowCasters.end()) {
            casterIt->second.clear();
        }
        bool particleBloomSource = false;
        if (hasGpuParticles) {
            m_gpuParticles.Draw(view, view, particleIt->second);
            for (const RenderParticleEmitter& emitter : particleIt->second) {
                particleBloomSource = particleBloomSource
                    || emitter.descriptor.blend == Material::BlendMode::Additive
                    || emitter.descriptor.brightness > kBloomThreshold;
            }
            particleIt->second.clear();
        }
        if (postProcess) {
            RenderVolumeClouds(slot, viewMtx, projMtx, cameraEye, environment, lights, lightCount);
            RenderVolumeSmoke(slot, viewMtx, projMtx, cameraEye, lights, lightCount,
                              smokeVolumes, smokeVolumeCount);
            RunPostProcess(view, slot, particleBloomSource, effectsPtr);
        }
        DrawPrintStringOverlay(view, slot, postProcess);
        return;
    }

    // Shadow skinning shares u_bones with the scene path, so the uniform set
    // must exist before the first frame's shadow submissions.
    m_uniforms.EnsureReady();

    // Shadow depth pass (runs first; lower bgfx view id than the scene view).
    ShadowPassData shadow;
    m_shadowScratch.clear();
    m_shadowScratch.swap(pendingIt->second);
    pendingIt->second.reserve(m_shadowScratch.size());
    for (const MeshDrawCommand& command : m_shadowScratch) {
        if (command.effect != RenderEffect::ParticleBillboard) {
            pendingIt->second.push_back(command);
        }
    }
    RenderShadowPass(view, slot, viewMtx, projection, fovYDegrees, orthoHeight,
                      aspect, nearPlane, farPlane, lights, lightCount, shadow);
    pendingIt->second.clear();
    pendingIt->second.swap(m_shadowScratch);

    // Color/MSAA state is shared by every draw; depth-test, depth-write and
    // cull come per-material from its DrawOptions so callers can customize how
    // each surface occludes (see Material::DrawOptions). Sequential view mode
    // makes bgfx honor submission order, which is how DrawOptions::priority is
    // realized below.
    const std::uint64_t baseState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);

    // Shadow-map V flip is driven by whether the backend's render-target
    // origin differs from its presentation origin; bgfx exposes it as the
    // `originBottomLeft` capability. On DirectX/Vulkan (top-left origin) the
    // shadow map texcoords need to flip V to line up with the light clip-space
    // UV the shader derives, mirroring the post-process present path.
    const bool flipShadowV = !bgfx::getCaps()->originBottomLeft;

    // Render planar reflections from a camera mirrored across the receiver plane.
    slot.planarValid = false;
    PlanarReflection::Plane plane;
    const MeshDrawCommand* planarReceiver = nullptr;
    for (const MeshDrawCommand& cmd : pendingIt->second) {
        if (plane.valid || !cmd.material.planarReflection) {
            continue;
        }
        plane.point[0] = cmd.worldMatrix[12];
        plane.point[1] = cmd.worldMatrix[13];
        plane.point[2] = cmd.worldMatrix[14];
        plane.normal[0] = cmd.worldMatrix[8];
        plane.normal[1] = cmd.worldMatrix[9];
        plane.normal[2] = cmd.worldMatrix[10];
        plane.valid = true;
        planarReceiver = &cmd;
        break;
    }
    if (plane.valid && slot.planarView != kInvalidRenderView
        && m_planarReflection.EnsureTargets(slot.width, slot.height, slot.planar)
        && m_meshProgram.EnsureReady()) {
        float mirrorView[16];
        float mirrorProj[16];
        float clipPlane[4];
        if (PlanarReflection::BuildMirrorCamera(viewMtx, projMtx, plane,
                                                mirrorView, mirrorProj, clipPlane)) {
            // Same order as scene viewProj (see RayTrace path): clip = P * V.
            bx::mtxMul(slot.planarViewProj, mirrorView, mirrorProj);
            bgfx::setViewFrameBuffer(slot.planarView, slot.planar.framebuffer);
            bgfx::setViewRect(slot.planarView, 0, 0,
                              static_cast<std::uint16_t>(slot.planar.width),
                              static_cast<std::uint16_t>(slot.planar.height));
            bgfx::setViewClear(slot.planarView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                               SkyBackgroundRgba(environment), 1.0f, 0);
            bgfx::setViewTransform(slot.planarView, mirrorView, mirrorProj);
            bgfx::setViewMode(slot.planarView, bgfx::ViewMode::Sequential);
            bgfx::touch(slot.planarView);

            CameraView mirrorCam{};
            float invMirror[16];
            bx::mtxInverse(invMirror, mirrorView);
            mirrorCam.eye[0] = invMirror[12];
            mirrorCam.eye[1] = invMirror[13];
            mirrorCam.eye[2] = invMirror[14];
            ClusterGrid planarGrid;
            planarGrid.nearPlane = nearPlane;
            planarGrid.farPlane = farPlane;
            planarGrid.screenWidth = slot.planar.width;
            planarGrid.screenHeight = slot.planar.height;
            m_lightCuller.Assign(
                lights, lightCount, mirrorView, slot.planarViewProj, planarGrid);
            const bool planarClustersReady = m_uniforms.UpdateClustersCpu(
                slot.planarForwardPlus, m_lightCuller, planarGrid);
            m_skyRenderer.Draw(slot.planarView, mirrorView, mirrorProj, mirrorCam.eye,
                               environment, lights, lightCount, false);
            constexpr std::uint16_t kInstanceStride = sizeof(float) * 16;
            for (const MeshDrawCommand& cmd : pendingIt->second) {
                const bool selectedReceiver = &cmd == planarReceiver
                    || (planarReceiver != nullptr && planarReceiver->reflectionOwner != 0
                        && cmd.reflectionOwner == planarReceiver->reflectionOwner);
                if (selectedReceiver) {
                    continue;
                }
                MeshHandle meshHandle = cmd.mesh;
                if (!meshHandle.IsValid()) {
                    // Primitive path uses shape; batcher already resolved meshes
                    // into handles for custom meshes only �?skip unresolved.
                    continue;
                }
                const BgfxMeshStore::BgfxMesh* mesh = m_meshes.Get(meshHandle);
                if (mesh == nullptr) {
                    continue;
                }
                if (bgfx::getAvailInstanceDataBuffer(1, kInstanceStride) < 1) {
                    continue;
                }
                bgfx::InstanceDataBuffer idb;
                bgfx::allocInstanceDataBuffer(&idb, 1, kInstanceStride);
                std::memcpy(idb.data, cmd.worldMatrix, kInstanceStride);
                const bool particleBillboard = cmd.effect == RenderEffect::ParticleBillboard;
                const bool skinned = cmd.bonePalette != nullptr && cmd.boneCount > 0;
                if ((particleBillboard && !m_meshProgram.EnsureParticleReady())
                    || (skinned && !m_meshProgram.EnsureSkinnedReady())) {
                    continue;
                }
                if (planarClustersReady) {
                    m_uniforms.SelectClusters(slot.planarForwardPlus);
                } else {
                    m_uniforms.DisableClusters();
                }
                m_uniforms.ApplyLighting(&mirrorCam, lights, lightCount, &environment);
                m_uniforms.ApplyMaterial(
                    cmd.material, m_textureCache, flipShadowV,
                    BGFX_INVALID_HANDLE, nullptr, false,
                    BGFX_INVALID_HANDLE, nullptr, nullptr, nullptr, false, clipPlane);
                m_shadowMap.BindDisabled(m_textureCache.White());
                if (skinned) {
                    m_uniforms.BindBones(cmd.bonePalette, cmd.boneCount);
                }
                const bool blended = cmd.material.blend != Material::BlendMode::Opaque;
                bgfx::setState(baseState
                    | ToBgfxDepthTest(cmd.material.depthTest)
                    | ((cmd.material.depthWrite && !blended) ? BGFX_STATE_WRITE_Z : 0)
                    | ToBgfxCull(MirroredCull(cmd.material.cull))
                    | ToBgfxBlend(cmd.material.blend));
                bgfx::setVertexBuffer(0, mesh->vb);
                bgfx::setIndexBuffer(mesh->ib);
                float identity[16];
                bx::mtxIdentity(identity);
                bgfx::setTransform(identity);
                bgfx::setInstanceDataBuffer(&idb);
                bgfx::submit(slot.planarView,
                             particleBillboard ? m_meshProgram.ParticleProgram()
                             : skinned ? m_meshProgram.SkinnedProgram()
                                       : m_meshProgram.Program());
            }
            if (hasGpuParticles) {
                m_gpuParticles.Draw(
                    view, slot.planarView, particleIt->second, clipPlane);
            }
            slot.planarValid = true;
        }
    }

    // Capture the current opaque scene around the dominant reflective sphere.
    // Its six lower-id views finish before the mesh shader samples the cubemap.
    RenderReflectionCapture(view, slot, pendingIt->second,
                            hasGpuParticles ? &particleIt->second : nullptr,
                            lights, lightCount, environment,
                            smokeVolumes, smokeVolumeCount);

    ClusterGrid grid;
    grid.nearPlane = nearPlane;
    grid.farPlane = farPlane;
    grid.screenWidth = slot.width;
    grid.screenHeight = slot.height;
    float viewProj[16];
    bx::mtxMul(viewProj, viewMtx, projMtx);

    bool mainClustersReady = false;
    const bool gpuReady = m_useGpuLightCulling
        && slot.lightCullView != kInvalidRenderView
        && slot.mainForwardPlus.Valid()
        && m_gpuLightCuller.Supported()
        && m_gpuLightCuller.EnsureReady();
    if (gpuReady) {
        m_lightCuller.PackOnly(lights, lightCount);
        mainClustersReady = m_uniforms.PrepareClustersGpu(
            slot.mainForwardPlus, m_lightCuller, grid);
        if (mainClustersReady) {
            m_gpuLightCuller.Cull(
                slot.lightCullView, slot.mainForwardPlus.lightData,
                slot.mainForwardPlus.clusterRanges, slot.mainForwardPlus.lightIndices,
                static_cast<std::uint32_t>(m_lightCuller.Lights().size()),
                m_lightCuller.DirectionalCount(), viewMtx, viewProj, grid);
        }
    }
    if (!mainClustersReady) {
        m_lightCuller.Assign(lights, lightCount, viewMtx, viewProj, grid);
        mainClustersReady = m_uniforms.UpdateClustersCpu(
            slot.mainForwardPlus, m_lightCuller, grid);
        if (m_lightCuller.CappedAssignments() > 0
            || lightCount > ClusterGrid::kMaxPackedLights) {
            Debug::Logger::Debug(
                "Render",
                "Forward+: %u input lights (cap %u), %u cluster assignments dropped (per-cluster cap %u)",
                lightCount, ClusterGrid::kMaxPackedLights,
                m_lightCuller.CappedAssignments(), ClusterGrid::kMaxLightsPerCluster);
        }
    }

    // Group this frame's draws into instanced batches: identical (mesh,
    // material) pairs collapse into one bgfx instanced submit, while each
    // distinct material sets its uniforms exactly once. The batcher is
    // reused across windows and across frames, so a stable scene allocates
    // nothing here after the first frame (see RenderBatcher).
    bool anyRayTraced = false;
    bool bloomSource = false;
    for (const MeshDrawCommand& command : pendingIt->second) {
        if (command.rayTraced) {
            anyRayTraced = true;
        }
        bloomSource = bloomSource
            || command.material.blend == Material::BlendMode::Additive
            || (!command.material.lit && command.material.emissiveStrength > 0.0f)
            || command.material.emissiveStrength > kBloomThreshold;
    }
    if (hasGpuParticles) {
        for (const RenderParticleEmitter& emitter : particleIt->second) {
            bloomSource = bloomSource
                || emitter.descriptor.blend == Material::BlendMode::Additive
                || emitter.descriptor.brightness > kBloomThreshold;
        }
    }
    const bool reflectionActive = anyRayTraced && slot.reflectionValid;

    m_skinnedScratch.clear();
    m_batcher.BeginFrame();
    for (const MeshDrawCommand& command : pendingIt->second) {
        if (command.bonePalette != nullptr && command.boneCount > 0) {
            // Skinned meshes carry a per-draw bone palette and use the skinned
            // program; they are not instance-merged (each has its own palette).
            m_skinnedScratch.push_back(&command);
        } else {
            m_batcher.Add(command);
        }
    }
    m_batcher.Finish();
    RenderDepthPrepass(
        slot, m_batcher.Batches(), m_skinnedScratch,
        m_depthPrepassBatchScratch, m_depthPrepassSkinnedScratch);

    // Four columns of the world matrix per instance (64 bytes, a multiple of
    // 16 as bgfx requires); matches vs_mesh's i_data0..i_data3. The material
    // rides along as per-batch uniforms rather than per-instance data.
    constexpr std::uint16_t kInstanceStride = sizeof(float) * 16;

    const std::span<const RenderBatch> batches = m_batcher.Batches();
    for (std::size_t batchIndex = 0; batchIndex < batches.size(); ++batchIndex) {
        const RenderBatch& batch = batches[batchIndex];
        const bool particleBillboard = batch.effect == RenderEffect::ParticleBillboard;
        if ((particleBillboard && !m_meshProgram.ParticleReady())
            || (!particleBillboard && !m_meshProgram.Ready())) {
            continue;
        }
        const BgfxMeshStore::BgfxMesh* mesh = m_meshes.Get(batch.mesh);
        if (mesh == nullptr) {
            // Stale handle (e.g. mesh destroyed mid-frame): skip the whole
            // batch rather than corrupting the instance buffer.
            continue;
        }

        const auto requested = static_cast<std::uint32_t>(batch.commands.size());
        const std::uint32_t count = std::min(requested, bgfx::getAvailInstanceDataBuffer(requested, kInstanceStride));
        if (count == 0) {
            Debug::Logger::Debug("Render", "instance buffer exhausted; dropped %u of %u instances",
                                 requested, requested);
            continue; // instance buffer pool exhausted this frame
        }
        if (count < requested) {
            Debug::Logger::Debug("Render", "instance buffer near full; drew %u of %u instances",
                                 count, requested);
        }

        bgfx::InstanceDataBuffer idb;
        bgfx::allocInstanceDataBuffer(&idb, count, kInstanceStride);
        std::uint8_t* cursor = idb.data;
        for (std::uint32_t i = 0; i < count; ++i) {
            // Pack the world matrix as four columns to match the vertex shader input.
            std::memcpy(cursor, batch.commands[i]->worldMatrix, kInstanceStride);
            cursor += kInstanceStride;
        }

        // Alpha and additive batches test against opaque depth without updating it,
        // preserving overlap between transparent surfaces.
        const bool blended = batch.material.blend != Material::BlendMode::Opaque;
        const bool prepassed = batchIndex < m_depthPrepassBatchScratch.size()
            && m_depthPrepassBatchScratch[batchIndex] != 0;
        const bool writeDepth = batch.material.depthWrite && !blended && !prepassed;
        const std::uint64_t state = baseState
            | (prepassed ? BGFX_STATE_DEPTH_TEST_EQUAL
                         : ToBgfxDepthTest(batch.material.depthTest))
            | (writeDepth ? BGFX_STATE_WRITE_Z : 0)
            | ToBgfxCull(batch.material.cull)
            | ToBgfxBlend(batch.material.blend);

        // Each batch sets its lighting, material and shadow resources before submit.
        if (mainClustersReady) {
            m_uniforms.SelectClusters(slot.mainForwardPlus);
        } else {
            m_uniforms.DisableClusters();
        }
        m_uniforms.ApplyLighting(camera, lights, lightCount, &environment);
        {
            bgfx::TextureHandle planarTex = BGFX_INVALID_HANDLE;
            const float* planarVp = nullptr;
            if (slot.planarValid) {
                planarTex = slot.planar.color;
                planarVp = slot.planarViewProj;
            }
            bgfx::TextureHandle sceneReflection = BGFX_INVALID_HANDLE;
            if (slot.reflectionValid) {
                sceneReflection = slot.reflection.color;
            }
            m_uniforms.ApplyMaterial(
                batch.material, m_textureCache, flipShadowV, planarTex, planarVp, false,
                sceneReflection,
                slot.reflectionValid ? slot.reflectionProbe : nullptr,
                slot.reflectionBoxValid ? slot.reflectionBoxMin : nullptr,
                slot.reflectionBoxValid ? slot.reflectionBoxMax : nullptr,
                reflectionActive && batch.realtimeReflection);
        }
        if (shadow.valid) {
            m_shadowMap.BindForSampling(slot.shadowTextures, shadow.lightViewProj,
                                        shadow.cameraView, shadow.lightDir,
                                         shadow.splitDepths, shadow.blendWidths,
                                         shadow.penumbraScaleTexels,
                                         shadow.normalBiasWorld,
                                         shadow.casterIndex, m_shadowConfig);
        } else {
            m_shadowMap.BindDisabled(m_textureCache.White());
        }
        bgfx::setState(state);
        bgfx::setVertexBuffer(0, mesh->vb);
        bgfx::setIndexBuffer(mesh->ib);
        // Instance data carries the full world matrix. Force the bgfx model
        // transform to identity so a leftover setTransform cannot compose with
        // the instance matrix (that composition is the classic "mesh swims
        // with the camera" failure mode).
        float identity[16];
        bx::mtxIdentity(identity);
        bgfx::setTransform(identity);
        bgfx::setInstanceDataBuffer(&idb);
        bgfx::submit(view, particleBillboard
            ? m_meshProgram.ParticleProgram()
            : m_meshProgram.Program());
    }

    // Skinned mesh pass: GPU linear-blend skinning. Each draw uploads its own
    // bone palette to u_bones and submits with the skinned program; not
    // instance-merged since palettes differ. The skinned program is compiled
    // lazily the first frame a skinned mesh appears.
    if (!m_skinnedScratch.empty() && m_meshProgram.EnsureSkinnedReady()) {
        for (std::size_t skinnedIndex = 0;
             skinnedIndex < m_skinnedScratch.size(); ++skinnedIndex) {
            const MeshDrawCommand* command = m_skinnedScratch[skinnedIndex];
            const BgfxMeshStore::BgfxMesh* mesh = m_meshes.Get(command->mesh);
            if (mesh == nullptr || !mesh->skinned) {
                continue;
            }
            if (bgfx::getAvailInstanceDataBuffer(1, kInstanceStride) < 1) {
                continue;
            }
            const bool blended = command->material.blend != Material::BlendMode::Opaque;
            const bool prepassed = skinnedIndex < m_depthPrepassSkinnedScratch.size()
                && m_depthPrepassSkinnedScratch[skinnedIndex] != 0;
            const bool writeDepth = command->material.depthWrite && !blended && !prepassed;
            const std::uint64_t state = baseState
                | (prepassed ? BGFX_STATE_DEPTH_TEST_EQUAL
                             : ToBgfxDepthTest(command->material.depthTest))
                | (writeDepth ? BGFX_STATE_WRITE_Z : 0)
                | ToBgfxCull(command->material.cull)
                | ToBgfxBlend(command->material.blend);

            if (mainClustersReady) {
                m_uniforms.SelectClusters(slot.mainForwardPlus);
            } else {
                m_uniforms.DisableClusters();
            }
            m_uniforms.ApplyLighting(camera, lights, lightCount, &environment);
            {
                bgfx::TextureHandle planarTex = BGFX_INVALID_HANDLE;
                const float* planarVp = nullptr;
                if (slot.planarValid) {
                    planarTex = slot.planar.color;
                    planarVp = slot.planarViewProj;
                }
                bgfx::TextureHandle sceneReflection = BGFX_INVALID_HANDLE;
                if (slot.reflectionValid) {
                    sceneReflection = slot.reflection.color;
                }
                m_uniforms.ApplyMaterial(
                    command->material, m_textureCache, flipShadowV, planarTex, planarVp, false,
                    sceneReflection,
                    slot.reflectionValid ? slot.reflectionProbe : nullptr,
                    slot.reflectionBoxValid ? slot.reflectionBoxMin : nullptr,
                    slot.reflectionBoxValid ? slot.reflectionBoxMax : nullptr,
                    reflectionActive && command->rayTraced);
            }
            if (shadow.valid) {
                m_shadowMap.BindForSampling(slot.shadowTextures, shadow.lightViewProj,
                                            shadow.cameraView, shadow.lightDir,
                                             shadow.splitDepths, shadow.blendWidths,
                                             shadow.penumbraScaleTexels,
                                             shadow.normalBiasWorld,
                                             shadow.casterIndex, m_shadowConfig);
            } else {
                m_shadowMap.BindDisabled(m_textureCache.White());
            }
            m_uniforms.BindBones(command->bonePalette, command->boneCount);

            bgfx::InstanceDataBuffer idb;
            bgfx::allocInstanceDataBuffer(&idb, 1, kInstanceStride);
            std::memcpy(idb.data, command->worldMatrix, kInstanceStride);

            bgfx::setState(state);
            bgfx::setVertexBuffer(0, mesh->vb);
            bgfx::setIndexBuffer(mesh->ib);
            float identity[16];
            bx::mtxIdentity(identity);
            bgfx::setTransform(identity);
            bgfx::setInstanceDataBuffer(&idb);
            bgfx::submit(view, m_meshProgram.SkinnedProgram());
        }
    }

    if (hasGpuParticles) {
        m_gpuParticles.Draw(view, view, particleIt->second);
    }

    // Water composites onto the resolved opaque scene and writes depth, so the
    // cloud and smoke passes that follow are still occluded by the surface.
    // Volumetric clouds composite into the HDR scene color, truncated by scene
    // depth, before bloom and the tone-mapped present.
    if (postProcess) {
        RenderVolumeClouds(slot, viewMtx, projMtx, cameraEye, environment, lights, lightCount);
        RenderVolumeSmoke(slot, viewMtx, projMtx, cameraEye, lights, lightCount,
                          smokeVolumes, smokeVolumeCount);
    }

    // Post-process: resolve the offscreen scene into the window with the AA pass.
    if (postProcess) {
        RunPostProcess(view, slot, bloomSource, effectsPtr);
    }

    DrawPrintStringOverlay(view, slot, postProcess);
    MaybeCaptureFrame(slot);

    pendingIt->second.clear();
    if (const auto casterIt = m_pendingShadowCasters.find(view);
        casterIt != m_pendingShadowCasters.end()) {
        casterIt->second.clear();
    }
    if (hasGpuParticles) {
        particleIt->second.clear();
    }
}

void BgfxRenderBackend::RenderDepthPrepass(
    ViewSlot& slot, std::span<const RenderBatch> batches,
    const std::vector<const MeshDrawCommand*>& skinned,
    std::vector<std::uint8_t>& batchResults,
    std::vector<std::uint8_t>& skinnedResults)
{
    batchResults.assign(batches.size(), 0);
    skinnedResults.assign(skinned.size(), 0);
    if (slot.depthPrepassView == kInvalidRenderView
        || !m_shadowMap.EnsureReady() || !m_meshProgram.Ready()) {
        return;
    }

    constexpr std::uint16_t kInstanceStride = sizeof(float) * 16;
    float identity[16];
    bx::mtxIdentity(identity);

    for (std::size_t batchIndex = 0; batchIndex < batches.size(); ++batchIndex) {
        const RenderBatch& batch = batches[batchIndex];
        if (!ParticipatesInDepthPrepass(batch.material, batch.effect)) {
            continue;
        }
        const BgfxMeshStore::BgfxMesh* mesh = m_meshes.Get(batch.mesh);
        if (mesh == nullptr) {
            continue;
        }
        const std::uint32_t requested = static_cast<std::uint32_t>(batch.commands.size());
        if (requested == 0
            || bgfx::getAvailInstanceDataBuffer(requested, kInstanceStride) < requested) {
            continue;
        }
        bgfx::InstanceDataBuffer instanceData;
        bgfx::allocInstanceDataBuffer(&instanceData, requested, kInstanceStride);
        std::uint8_t* cursor = instanceData.data;
        for (std::uint32_t index = 0; index < requested; ++index) {
            std::memcpy(cursor, batch.commands[index]->worldMatrix, kInstanceStride);
            cursor += kInstanceStride;
        }
        bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
                       | ToBgfxCull(batch.material.cull));
        bgfx::setVertexBuffer(0, mesh->vb);
        bgfx::setIndexBuffer(mesh->ib);
        bgfx::setTransform(identity);
        bgfx::setInstanceDataBuffer(&instanceData);
        bgfx::submit(slot.depthPrepassView, m_shadowMap.Program());
        batchResults[batchIndex] = 1;
    }

    if (skinned.empty() || !m_meshProgram.EnsureSkinnedReady()) {
        return;
    }
    for (std::size_t commandIndex = 0; commandIndex < skinned.size(); ++commandIndex) {
        const MeshDrawCommand* command = skinned[commandIndex];
        if (!ParticipatesInDepthPrepass(command->material, command->effect)) {
            continue;
        }
        const BgfxMeshStore::BgfxMesh* mesh = m_meshes.Get(command->mesh);
        if (mesh == nullptr || !mesh->skinned
            || bgfx::getAvailInstanceDataBuffer(1, kInstanceStride) < 1) {
            continue;
        }
        m_uniforms.BindBones(command->bonePalette, command->boneCount);
        bgfx::InstanceDataBuffer instanceData;
        bgfx::allocInstanceDataBuffer(&instanceData, 1, kInstanceStride);
        std::memcpy(instanceData.data, command->worldMatrix, kInstanceStride);
        bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS
                       | ToBgfxCull(command->material.cull));
        bgfx::setVertexBuffer(0, mesh->vb);
        bgfx::setIndexBuffer(mesh->ib);
        bgfx::setTransform(identity);
        bgfx::setInstanceDataBuffer(&instanceData);
        bgfx::submit(slot.depthPrepassView, m_shadowMap.SkinnedProgram());
        skinnedResults[commandIndex] = 1;
    }
}

void BgfxRenderBackend::RenderVolumeClouds(ViewSlot& slot, const float viewMatrix[16],
                                           const float projectionMatrix[16], const float eye[3],
                                           const SkyEnvironment& environment,
                                           const RenderLight* lights, std::uint32_t lightCount)
{
    if (slot.cloudView == kInvalidRenderView || slot.cloudCompositeView == kInvalidRenderView
        || !bgfx::isValid(slot.cloudFb) || !bgfx::isValid(slot.cloudColor)
        || !bgfx::isValid(slot.scene.framebuffer) || !bgfx::isValid(slot.scene.depth)
        || !BgfxVolumeCloudRenderer::Enabled(environment)) {
        return;
    }
    BgfxVolumeCloudRenderer::DrawParams params;
    params.marchView = slot.cloudView;
    params.compositeView = slot.cloudCompositeView;
    params.lowResFb = slot.cloudFb;
    params.lowResColor = slot.cloudColor;
    params.lowWidth = slot.cloudWidth;
    params.lowHeight = slot.cloudHeight;
    params.sceneFb = slot.scene.framebuffer;
    params.sceneDepth = slot.scene.depth;
    params.fullWidth = slot.width;
    params.fullHeight = slot.height;
    params.viewMatrix = viewMatrix;
    params.projectionMatrix = projectionMatrix;
    params.eye = eye;
    params.lights = lights;
    params.lightCount = lightCount;
    m_volumeCloud.Draw(params, environment);
}

void BgfxRenderBackend::RenderVolumeSmoke(ViewSlot& slot, const float viewMatrix[16],
                                          const float projectionMatrix[16], const float eye[3],
                                          const RenderLight* lights, std::uint32_t lightCount,
                                          const RenderSmokeVolume* volumes,
                                          std::uint32_t volumeCount)
{
    if (slot.smokeView == kInvalidRenderView || slot.smokeCompositeView == kInvalidRenderView
        || volumeCount == 0 || volumes == nullptr
        || !bgfx::isValid(slot.smokeFb) || !bgfx::isValid(slot.smokeColor)
        || !bgfx::isValid(slot.smokeDepthProxy)
        || !bgfx::isValid(slot.scene.framebuffer) || !bgfx::isValid(slot.scene.depth)) {
        return;
    }
    BgfxSmokeRenderer::DrawParams params;
    params.marchView = slot.smokeView;
    params.compositeView = slot.smokeCompositeView;
    params.sceneFb = slot.scene.framebuffer;
    params.sceneDepth = slot.scene.depth;
    params.lowResFb = slot.smokeFb;
    params.lowResColor = slot.smokeColor;
    params.lowResDepth = slot.smokeDepthProxy;
    params.lowWidth = slot.smokeWidth;
    params.lowHeight = slot.smokeHeight;
    params.fullWidth = slot.width;
    params.fullHeight = slot.height;
    params.viewMatrix = viewMatrix;
    params.projectionMatrix = projectionMatrix;
    params.eye = eye;
    params.lights = lights;
    params.lightCount = lightCount;
    params.volumes = volumes;
    params.volumeCount = volumeCount;
    m_volumeSmoke.Draw(params);
}

void BgfxRenderBackend::DrawPrintStringOverlay(RenderViewHandle view, const ViewSlot& slot,
                                               bool postProcess)
{
    // Target the Game framebuffer because bgfx debug text uses the hidden
    // process backbuffer.
    std::vector<Detail::PrintStringLine> lines;
    Detail::SnapshotPrintStrings(lines);
    std::vector<Detail::PrintStringLine> overlayLines;
    Debug::Detail::SnapshotDebugOverlay(overlayLines);
    UI::DrawList uiList;
    UI::Detail::SnapshotDrawList(slot.window, uiList);
    if (lines.empty() && overlayLines.empty() && uiList.Empty()) {
        return;
    }
    const RenderViewHandle overlayView =
        postProcess && slot.presentView != kInvalidRenderView ? slot.presentView : view;
    // Sequential so the overlay lands after the present blit in the same view.
    bgfx::setViewMode(overlayView, bgfx::ViewMode::Sequential);

    // Game UI (panels/labels/buttons) draws first, so the developer HUD and
    // PrintString notifications below stay legible on top of it.
    if (!uiList.Empty() && m_uiRenderer.EnsureReady()) {
        m_uiRenderer.Draw(overlayView, slot.width, slot.height, uiList, &m_textureCache);
    }

    // PrintString and the debug HUD need the text overlay; if there is nothing
    // to write or it is not ready, the UI above has still drawn.
    if ((lines.empty() && overlayLines.empty()) || !m_debugText.EnsureReady()) {
        return;
    }

    // Timed PrintString notifications stack at the bottom-left.
    if (!lines.empty()) {
        std::vector<DebugTextLine> drawLines;
        drawLines.reserve(lines.size());
        for (const auto& line : lines) {
            drawLines.push_back(DebugTextLine{line.text, line.color});
        }
        m_debugText.Draw(overlayView, slot.width, slot.height, drawLines,
                         DebugTextOverlay::Anchor::BottomLeft);
    }

    // The persistent debug HUD (FPS/time/etc.) sits in the top-right corner.
    if (!overlayLines.empty()) {
        std::vector<DebugTextLine> hudLines;
        hudLines.reserve(overlayLines.size());
        for (const auto& line : overlayLines) {
            hudLines.push_back(DebugTextLine{line.text, line.color});
        }
        m_debugText.Draw(overlayView, slot.width, slot.height, hudLines,
                         DebugTextOverlay::Anchor::TopRight);
    }
}

} // namespace Concord
