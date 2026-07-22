#include "engine/render/backend/BgfxRenderBackend.h"

#include "engine/render/backend/BgfxMathConverters.h"
#include "engine/render/backend/BgfxSceneAabb.h"
#include "engine/render/batch/MaterialHash.h"
#include "engine/render/frame/SkyEnvironment.h"
#include "engine/render/reflection/ReflectionReceiverSelector.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace {

namespace Detail = Concord::RenderDetail;
using Detail::ToBgfxBlend;
using Detail::ToBgfxCull;
using Detail::ToBgfxDepthTest;
using Detail::TransformAabbWorld;
using Detail::TransformSkinnedAabbWorld;

constexpr float kCaptureNear = 0.05f;
constexpr float kCaptureFar = 100.0f;
/** Cubemap faces refreshed per frame once the probe has been fully filled once. */
constexpr std::uint32_t kFacesPerFrame = 2;
constexpr std::uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
constexpr std::uint64_t kFnvPrime = 0x100000001b3ULL;

/** Computes one draw's conservative world bounds, including its current skin pose. */
void ComputeCommandWorldBounds(float outMin[3], float outMax[3],
                               const Concord::MeshDrawCommand& command,
                               const Concord::BgfxMeshStore::BgfxMesh& mesh) noexcept
{
    const std::uint32_t boneCount = command.bonePalette == nullptr
        ? 0u : std::min(command.boneCount, Concord::kMaxRenderBones);
    if (boneCount > 0 && !mesh.boneAabbs.empty()) {
        TransformSkinnedAabbWorld(
            outMin, outMax, mesh.boneAabbs.data(),
            static_cast<std::uint32_t>(mesh.boneAabbs.size()),
            command.bonePalette, boneCount, command.worldMatrix);
        if (outMin[0] <= outMax[0]) {
            return;
        }
    }
    TransformAabbWorld(outMin, outMax, mesh.aabbMin, mesh.aabbMax,
                       command.worldMatrix);
}

/** Folds one integer value into a stable reflection-scene signature. */
void MixSignature(std::uint64_t& signature, std::uint64_t value) noexcept
{
    signature ^= value;
    signature *= kFnvPrime;
}

/** Folds a float's exact bit pattern into a reflection-scene signature. */
void MixSignatureFloat(std::uint64_t& signature, float value) noexcept
{
    MixSignature(signature, std::bit_cast<std::uint32_t>(value));
}

/** Folds a contiguous float array into a reflection-scene signature. */
void MixSignatureFloats(std::uint64_t& signature, const float* values,
                        std::size_t count) noexcept
{
    for (std::size_t index = 0; index < count; ++index) {
        MixSignatureFloat(signature, values[index]);
    }
}

/** Returns true when a world AABB can overlap a 90-degree cubemap face. */
bool IntersectsCaptureFace(const float view[16], const float worldMin[3],
                           const float worldMax[3]) noexcept
{
    bool outsideNear = true;
    bool outsideFar = true;
    bool outsideLeft = true;
    bool outsideRight = true;
    bool outsideBottom = true;
    bool outsideTop = true;

    for (std::uint32_t xIndex = 0; xIndex < 2; ++xIndex) {
        for (std::uint32_t yIndex = 0; yIndex < 2; ++yIndex) {
            for (std::uint32_t zIndex = 0; zIndex < 2; ++zIndex) {
                const float wx = xIndex == 0 ? worldMin[0] : worldMax[0];
                const float wy = yIndex == 0 ? worldMin[1] : worldMax[1];
                const float wz = zIndex == 0 ? worldMin[2] : worldMax[2];
                const float x = wx * view[0] + wy * view[4] + wz * view[8] + view[12];
                const float y = wx * view[1] + wy * view[5] + wz * view[9] + view[13];
                const float z = wx * view[2] + wy * view[6] + wz * view[10] + view[14];

                outsideNear = outsideNear && z < kCaptureNear;
                outsideFar = outsideFar && z > kCaptureFar;
                outsideLeft = outsideLeft && x < -z;
                outsideRight = outsideRight && x > z;
                outsideBottom = outsideBottom && y < -z;
                outsideTop = outsideTop && y > z;
            }
        }
    }
    return !(outsideNear || outsideFar || outsideLeft || outsideRight
             || outsideBottom || outsideTop);
}

/** Computes the subset of cubemap faces which can see a world AABB. */
std::uint8_t CaptureFaceMask(
    const std::array<std::array<float, 16>, Concord::ReflectionCapture::kFaceCount>& views,
    const float worldMin[3], const float worldMax[3]) noexcept
{
    std::uint8_t mask = 0;
    for (std::uint32_t face = 0; face < Concord::ReflectionCapture::kFaceCount; ++face) {
        if (IntersectsCaptureFace(views[face].data(), worldMin, worldMax)) {
            mask = static_cast<std::uint8_t>(mask | (1u << face));
        }
    }
    return mask;
}

} // namespace

namespace Concord {

void BgfxRenderBackend::RenderReflectionCapture(
    RenderViewHandle ownerView, ViewSlot& slot,
    const std::vector<MeshDrawCommand>& commands,
    const std::vector<RenderParticleEmitter>* particles,
    const RenderLight* lights, std::uint32_t lightCount,
    const SkyEnvironment& environment,
    const RenderSmokeVolume* smokeVolumes, std::uint32_t smokeVolumeCount)
{
    slot.reflectionValid = false;
    slot.reflectionBoxValid = false;

    std::vector<ReflectionReceiverBounds> receiverCandidates;
    receiverCandidates.reserve(commands.size());
    for (std::size_t commandIndex = 0; commandIndex < commands.size(); ++commandIndex) {
        const MeshDrawCommand& command = commands[commandIndex];
        if (!command.rayTraced) {
            continue;
        }
        const BgfxMeshStore::BgfxMesh* mesh = m_meshes.Get(command.mesh);
        if (mesh == nullptr) {
            continue;
        }
        ReflectionReceiverBounds candidate;
        candidate.owner = command.reflectionOwner;
        candidate.commandIndex = commandIndex;
        ComputeCommandWorldBounds(candidate.boundsMin, candidate.boundsMax, command, *mesh);
        receiverCandidates.push_back(candidate);
    }
    const ReflectionReceiverSelection receiverSelection =
        SelectReflectionReceiver(receiverCandidates);
    if (!receiverSelection.valid || !m_meshProgram.EnsureReady()) {
        return;
    }
    float capturePosition[3]{};
    float receiverMin[3]{};
    float receiverMax[3]{};
    for (std::uint32_t axis = 0; axis < 3; ++axis) {
        receiverMin[axis] = receiverSelection.boundsMin[axis];
        receiverMax[axis] = receiverSelection.boundsMax[axis];
        capturePosition[axis] = (receiverMin[axis] + receiverMax[axis]) * 0.5f;
    }
    const bool targetsWereValid = slot.reflection.Valid();
    if (!m_reflectionCapture.EnsureTargets(slot.reflection)) {
        return;
    }
    if (!targetsWereValid) {
        slot.reflectionSignatureValid = false;
        slot.reflectionInitialized = false;
        slot.reflectionFaceCursor = 0;
        slot.reflectionPendingFaces = 0;
    }
    std::copy_n(capturePosition, 3, slot.reflectionProbe);

    std::uint64_t signature = kFnvOffsetBasis;
    MixSignature(signature, static_cast<std::uint64_t>(receiverSelection.owner));
    MixSignature(signature, static_cast<std::uint64_t>(receiverSelection.representativeIndex));
    MixSignatureFloats(signature, capturePosition, 3);
    MixSignatureFloats(signature, receiverMin, 3);
    MixSignatureFloats(signature, receiverMax, 3);
    MixSignature(signature, static_cast<std::uint64_t>(environment.mode));
    MixSignature(signature, environment.solidColor);
    MixSignature(signature, environment.zenithColor);
    MixSignature(signature, environment.horizonColor);
    MixSignature(signature, environment.groundColor);
    MixSignature(signature, environment.ambientColor);
    MixSignatureFloat(signature, environment.intensity);
    MixSignatureFloat(signature, environment.ambientIntensity);
    MixSignatureFloat(signature, environment.nightAmbientIntensity);
    MixSignatureFloat(signature, environment.horizonFalloff);
    MixSignatureFloat(signature, environment.sunDiskIntensity);
    MixSignature(signature, environment.sunDisk ? 1u : 0u);
    float enclosingMin[3]{};
    float enclosingMax[3]{};
    float enclosingVolume = std::numeric_limits<float>::max();
    bool enclosingFound = false;
    std::array<std::array<float, 2>, 3> slabSurface{};
    std::array<std::array<bool, 2>, 3> slabFound{};
    std::uint32_t capturedCommandCount = 0;
    for (const MeshDrawCommand& command : commands) {
        const std::size_t commandIndex =
            static_cast<std::size_t>(&command - commands.data());
        const BgfxMeshStore::BgfxMesh* mesh = m_meshes.Get(command.mesh);
        if (IsSelectedReflectionReceiver(
                receiverSelection, command.reflectionOwner, commandIndex)
            || mesh == nullptr) {
            continue;
        }
        MixSignature(signature, command.mesh.index);
        MixSignature(signature, command.mesh.generation);
        MixSignature(signature, HashMaterial(command.material));
        MixSignatureFloats(signature, command.worldMatrix, 16);
        const std::uint32_t boneCount = command.bonePalette == nullptr
            ? 0u : std::min(command.boneCount, kMaxRenderBones);
        MixSignature(signature, boneCount);
        if (boneCount > 0) {
            MixSignatureFloats(signature, command.bonePalette,
                               static_cast<std::size_t>(boneCount) * 16);
        }

        float worldMin[3];
        float worldMax[3];
        ComputeCommandWorldBounds(worldMin, worldMax, command, *mesh);

        constexpr float kBoundsEpsilon = 0.01f;
        bool containsReceiver = true;
        float extent[3];
        for (std::uint32_t axis = 0; axis < 3; ++axis) {
            extent[axis] = worldMax[axis] - worldMin[axis];
            containsReceiver = containsReceiver
                && worldMin[axis] <= receiverMin[axis] - kBoundsEpsilon
                && worldMax[axis] >= receiverMax[axis] + kBoundsEpsilon;
        }
        if (containsReceiver) {
            const float volume = extent[0] * extent[1] * extent[2];
            if (volume > 0.0f && volume < enclosingVolume) {
                enclosingVolume = volume;
                enclosingFound = true;
                std::copy_n(worldMin, 3, enclosingMin);
                std::copy_n(worldMax, 3, enclosingMax);
            }
        }

        for (std::uint32_t axis = 0; axis < 3; ++axis) {
            const std::uint32_t firstOther = (axis + 1) % 3;
            const std::uint32_t secondOther = (axis + 2) % 3;
            const float crossExtent = std::min(extent[firstOther], extent[secondOther]);
            const float maxThickness = std::max(0.5f, crossExtent * 0.15f);
            const bool coversReceiverCrossSection =
                worldMin[firstOther] <= receiverMin[firstOther] - kBoundsEpsilon
                && worldMax[firstOther] >= receiverMax[firstOther] + kBoundsEpsilon
                && worldMin[secondOther] <= receiverMin[secondOther] - kBoundsEpsilon
                && worldMax[secondOther] >= receiverMax[secondOther] + kBoundsEpsilon;
            if (!coversReceiverCrossSection || extent[axis] > maxThickness) {
                continue;
            }

            if (worldMax[axis] <= receiverMin[axis] + kBoundsEpsilon
                && (!slabFound[axis][0]
                    || worldMax[axis] > slabSurface[axis][0])) {
                slabSurface[axis][0] = worldMax[axis];
                slabFound[axis][0] = true;
            }
            if (worldMin[axis] >= receiverMax[axis] - kBoundsEpsilon
                && (!slabFound[axis][1]
                    || worldMin[axis] < slabSurface[axis][1])) {
                slabSurface[axis][1] = worldMin[axis];
                slabFound[axis][1] = true;
            }
        }
        ++capturedCommandCount;
    }
    MixSignature(signature, capturedCommandCount);

    bool completeSlabBox = true;
    for (std::uint32_t axis = 0; axis < 3; ++axis) {
        completeSlabBox = completeSlabBox
            && slabFound[axis][0] && slabFound[axis][1]
            && slabSurface[axis][0] < slabSurface[axis][1];
    }
    // Do not infer a parallax box from arbitrary scene AABBs. False-positive
    // room/slab detection warps one reflected object into displaced fragments.
    // Box projection will be re-enabled only for an explicitly authored probe.
    (void)completeSlabBox;
    (void)enclosingFound;

    const std::uint32_t capturedLightCount = lights == nullptr ? 0u : lightCount;
    MixSignature(signature, capturedLightCount);
    for (std::uint32_t index = 0; index < capturedLightCount; ++index) {
        const RenderLight& light = lights[index];
        MixSignature(signature, static_cast<std::uint64_t>(light.type));
        MixSignatureFloats(signature, light.position, 3);
        MixSignatureFloats(signature, light.direction, 3);
        MixSignature(signature, light.color);
        MixSignatureFloat(signature, light.intensity);
        MixSignatureFloat(signature, light.range);
        MixSignatureFloat(signature, light.sourceRadius);
        MixSignatureFloat(signature, light.directionalAngularRadiusDegrees);
        MixSignatureFloat(signature, light.innerAngleDegrees);
        MixSignatureFloat(signature, light.outerAngleDegrees);
    }

    // Fold the smoke volumes into the signature. Their `windOffset` advances
    // every frame while animating, so the signature keeps changing and the
    // amortized refresh cycle never fully settles — the reflection tracks the
    // drifting smoke a couple of faces per frame. A static (frozen) volume has a
    // constant offset, so a settled scene still early-outs.
    const std::uint32_t capturedSmokeCount = smokeVolumes == nullptr
        ? 0u : std::min(smokeVolumeCount, kMaxRenderSmokeVolumes);
    MixSignature(signature, capturedSmokeCount);
    for (std::uint32_t index = 0; index < capturedSmokeCount; ++index) {
        const RenderSmokeVolume& volume = smokeVolumes[index];
        MixSignatureFloats(signature, volume.boxMin, 3);
        MixSignatureFloats(signature, volume.boxMax, 3);
        MixSignatureFloats(signature, volume.windOffset, 3);
        MixSignature(signature, volume.color);
        MixSignatureFloat(signature, volume.density);
    }

    const std::uint32_t particleCount = particles == nullptr
        ? 0u : static_cast<std::uint32_t>(particles->size());
    MixSignature(signature, particleCount);
    if (particles != nullptr) {
        for (const RenderParticleEmitter& emitter : *particles) {
            MixSignature(signature, emitter.emitterKey);
            MixSignature(signature, emitter.emitterId);
            MixSignature(signature, emitter.spawnSequence);
            std::uint64_t timeBits = 0;
            std::memcpy(&timeBits, &emitter.simulationTime, sizeof(timeBits));
            MixSignature(signature, timeBits);
            MixSignatureFloats(signature, emitter.world, 16);
        }
    }

    if (slot.reflectionInitialized && slot.reflectionSignatureValid
        && slot.reflectionSignature == signature) {
        // Scene unchanged and every face already reflects it: nothing to redo.
        slot.reflectionValid = true;
        return;
    }

    // A changed scene restarts the refresh cycle. Faces are redrawn a few per
    // frame; only once all faces have been redrawn under the current signature is
    // it cached again, letting a settled scene early-out above on later frames.
    if (!slot.reflectionSignatureValid || slot.reflectionPendingSignature != signature) {
        slot.reflectionPendingSignature = signature;
        slot.reflectionPendingFaces = ReflectionCapture::kFaceCount;
    }
    slot.reflectionSignatureValid = false;

    // The very first fill draws all faces so the cubemap is complete before it is
    // sampled; afterwards only a slice of faces refreshes per frame, and the rest
    // keep their previous content.
    const bool bootstrap = !slot.reflectionInitialized;
    const std::uint32_t faceRenderCount = bootstrap
        ? ReflectionCapture::kFaceCount
        : std::min<std::uint32_t>(kFacesPerFrame, ReflectionCapture::kFaceCount);
    std::uint8_t renderFaceMask = 0;
    for (std::uint32_t index = 0; index < faceRenderCount; ++index) {
        const std::uint32_t face = bootstrap
            ? index
            : (slot.reflectionFaceCursor + index) % ReflectionCapture::kFaceCount;
        renderFaceMask = static_cast<std::uint8_t>(renderFaceMask | (1u << face));
    }

    constexpr std::uint16_t kInstanceStride = sizeof(float) * 16;
    constexpr std::uint16_t kCaptureSize =
        static_cast<std::uint16_t>(ReflectionCapture::kResolution);
    const bool flipShadowV = !bgfx::getCaps()->originBottomLeft;
    const bool homogeneousDepth = bgfx::getCaps()->homogeneousDepth;
    const std::uint64_t baseState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;

    CameraView captureCamera{};
    std::copy_n(capturePosition, 3, captureCamera.eye);

    std::array<std::array<float, 16>, ReflectionCapture::kFaceCount> faceViews{};
    std::array<std::array<float, 16>, ReflectionCapture::kFaceCount> faceProjections{};
    for (std::uint32_t face = 0; face < ReflectionCapture::kFaceCount; ++face) {
        if (!ReflectionCapture::BuildFaceCamera(
                face, capturePosition, homogeneousDepth,
                faceViews[face].data(), faceProjections[face].data())) {
            return;
        }
    }

    m_reflectionVisibilityScratch.assign(commands.size(), 0);
    m_skinnedScratch.clear();
    m_batcher.BeginFrame();
    for (const MeshDrawCommand& command : commands) {
        const std::size_t commandIndex =
            static_cast<std::size_t>(&command - commands.data());
        if (IsSelectedReflectionReceiver(
                receiverSelection, command.reflectionOwner, commandIndex)
            ) {
            continue;
        }
        const BgfxMeshStore::BgfxMesh* mesh = m_meshes.Get(command.mesh);
        if (mesh == nullptr) {
            continue;
        }
        const bool skinned = command.bonePalette != nullptr && command.boneCount > 0;
        float worldMin[3];
        float worldMax[3];
        ComputeCommandWorldBounds(worldMin, worldMax, command, *mesh);
        const std::uint8_t faceMask = CaptureFaceMask(faceViews, worldMin, worldMax);
        m_reflectionVisibilityScratch[commandIndex] = faceMask;
        if (faceMask == 0) {
            continue;
        }
        if (skinned) {
            m_skinnedScratch.push_back(&command);
        } else {
            m_batcher.Add(command);
        }
    }
    m_batcher.Finish();

    const bool skinnedReady = m_skinnedScratch.empty() || m_meshProgram.EnsureSkinnedReady();
    bool captureComplete = skinnedReady;
    float identity[16];
    bx::mtxIdentity(identity);

    for (std::uint32_t face = 0; face < ReflectionCapture::kFaceCount; ++face) {
        if ((renderFaceMask & (1u << face)) == 0) {
            continue;
        }
        const RenderViewHandle captureView = slot.reflectionViews[face];
        bgfx::setViewFrameBuffer(captureView, slot.reflection.framebuffers[face]);
        bgfx::setViewRect(captureView, 0, 0, kCaptureSize, kCaptureSize);
        bgfx::setViewClear(captureView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                           SkyReflectionClearRgba(environment), 1.0f, 0);
        bgfx::setViewTransform(captureView, faceViews[face].data(),
                              faceProjections[face].data());
        bgfx::setViewMode(captureView, bgfx::ViewMode::Sequential);
        bgfx::touch(captureView);
        ClusterGrid captureGrid;
        captureGrid.nearPlane = kCaptureNear;
        captureGrid.farPlane = kCaptureFar;
        captureGrid.screenWidth = ReflectionCapture::kResolution;
        captureGrid.screenHeight = ReflectionCapture::kResolution;
        float faceViewProjection[16];
        bx::mtxMul(faceViewProjection, faceViews[face].data(), faceProjections[face].data());
        m_lightCuller.Assign(
            lights, lightCount, faceViews[face].data(), faceViewProjection, captureGrid);
        const bool faceClustersReady = m_uniforms.UpdateClustersCpu(
            slot.reflectionForwardPlus[face], m_lightCuller, captureGrid);
        m_skyRenderer.Draw(captureView, faceViews[face].data(),
                           faceProjections[face].data(), captureCamera.eye,
                           environment, lights, lightCount, true);

        const std::uint8_t faceBit = static_cast<std::uint8_t>(1u << face);
        for (const RenderBatch& batch : m_batcher.Batches()) {
            const bool particleBillboard = batch.effect == RenderEffect::ParticleBillboard;
            if (particleBillboard && !m_meshProgram.EnsureParticleReady()) {
                captureComplete = false;
                continue;
            }
            const BgfxMeshStore::BgfxMesh* mesh = m_meshes.Get(batch.mesh);
            if (mesh == nullptr) {
                continue;
            }
            std::uint32_t requested = 0;
            for (const MeshDrawCommand* command : batch.commands) {
                const std::size_t commandIndex =
                    static_cast<std::size_t>(command - commands.data());
                if ((m_reflectionVisibilityScratch[commandIndex] & faceBit) != 0) {
                    ++requested;
                }
            }
            if (requested == 0) {
                continue;
            }
            const std::uint32_t count = std::min(
                requested, bgfx::getAvailInstanceDataBuffer(requested, kInstanceStride));
            if (count == 0) {
                captureComplete = false;
                continue;
            }
            captureComplete = captureComplete && count == requested;

            if (faceClustersReady) {
                m_uniforms.SelectClusters(slot.reflectionForwardPlus[face]);
            } else {
                m_uniforms.DisableClusters();
            }
            m_uniforms.ApplyLighting(&captureCamera, lights, lightCount, &environment);
            m_uniforms.ApplyMaterial(batch.material, m_textureCache, flipShadowV,
                                     BGFX_INVALID_HANDLE, nullptr, true);
            // Main-camera cascades do not cover a 360-degree probe and would
            // bake cascade boundaries or stale depth into the reflection.
            m_shadowMap.BindDisabled(m_textureCache.White());
            bgfx::InstanceDataBuffer instanceData;
            bgfx::allocInstanceDataBuffer(&instanceData, count, kInstanceStride);
            std::uint8_t* cursor = instanceData.data;
            std::uint32_t copied = 0;
            for (const MeshDrawCommand* command : batch.commands) {
                const std::size_t commandIndex =
                    static_cast<std::size_t>(command - commands.data());
                if ((m_reflectionVisibilityScratch[commandIndex] & faceBit) == 0) {
                    continue;
                }
                std::memcpy(cursor, command->worldMatrix, kInstanceStride);
                cursor += kInstanceStride;
                if (++copied == count) {
                    break;
                }
            }

            const bool blended = batch.material.blend != Material::BlendMode::Opaque;
            const std::uint64_t state = baseState
                | ToBgfxDepthTest(batch.material.depthTest)
                | ((batch.material.depthWrite && !blended) ? BGFX_STATE_WRITE_Z : 0)
                | ToBgfxCull(batch.material.cull)
                | ToBgfxBlend(batch.material.blend);
            bgfx::setState(state);
            bgfx::setVertexBuffer(0, mesh->vb);
            bgfx::setIndexBuffer(mesh->ib);
            bgfx::setTransform(identity);
            bgfx::setInstanceDataBuffer(&instanceData);
            bgfx::submit(captureView, particleBillboard
                ? m_meshProgram.ParticleProgram() : m_meshProgram.Program());
        }

        if (!skinnedReady) {
            continue;
        }
        for (const MeshDrawCommand* command : m_skinnedScratch) {
            const std::size_t commandIndex =
                static_cast<std::size_t>(command - commands.data());
            if ((m_reflectionVisibilityScratch[commandIndex] & faceBit) == 0) {
                continue;
            }
            const BgfxMeshStore::BgfxMesh* mesh = m_meshes.Get(command->mesh);
            if (mesh == nullptr || !mesh->skinned
                || bgfx::getAvailInstanceDataBuffer(1, kInstanceStride) < 1) {
                captureComplete = false;
                continue;
            }
            if (faceClustersReady) {
                m_uniforms.SelectClusters(slot.reflectionForwardPlus[face]);
            } else {
                m_uniforms.DisableClusters();
            }
            m_uniforms.ApplyLighting(&captureCamera, lights, lightCount, &environment);
            m_uniforms.ApplyMaterial(command->material, m_textureCache, flipShadowV,
                                     BGFX_INVALID_HANDLE, nullptr, true);
            m_shadowMap.BindDisabled(m_textureCache.White());
            m_uniforms.BindBones(command->bonePalette, command->boneCount);

            bgfx::InstanceDataBuffer instanceData;
            bgfx::allocInstanceDataBuffer(&instanceData, 1, kInstanceStride);
            std::memcpy(instanceData.data, command->worldMatrix, kInstanceStride);

            const bool blended = command->material.blend != Material::BlendMode::Opaque;
            const std::uint64_t state = baseState
                | ToBgfxDepthTest(command->material.depthTest)
                | ((command->material.depthWrite && !blended) ? BGFX_STATE_WRITE_Z : 0)
                | ToBgfxCull(command->material.cull)
                | ToBgfxBlend(command->material.blend);
            bgfx::setState(state);
            bgfx::setVertexBuffer(0, mesh->vb);
            bgfx::setIndexBuffer(mesh->ib);
            bgfx::setTransform(identity);
            bgfx::setInstanceDataBuffer(&instanceData);
            bgfx::submit(captureView, m_meshProgram.SkinnedProgram());
        }

        if (particles != nullptr && !particles->empty()) {
            m_gpuParticles.Draw(ownerView, captureView, *particles);
        }

        // Composite local smoke into this face, sequenced after its geometry so
        // the reflective sphere shows the same drifting smoke the main view
        // does. The cubemap depth is write-only (not sampleable), so this pass
        // is not depth-truncated — a 1.0 white "depth" stands in — and uses
        // fewer march steps to keep the amortized per-face cost low.
        if (smokeVolumeCount > 0 && smokeVolumes != nullptr) {
            BgfxSmokeRenderer::DrawParams smokeParams;
            smokeParams.marchView = captureView;
            smokeParams.sceneDepth = m_textureCache.White();
            smokeParams.fullWidth = ReflectionCapture::kResolution;
            smokeParams.fullHeight = ReflectionCapture::kResolution;
            smokeParams.viewMatrix = faceViews[face].data();
            smokeParams.projectionMatrix = faceProjections[face].data();
            smokeParams.eye = captureCamera.eye;
            smokeParams.lights = lights;
            smokeParams.lightCount = lightCount;
            smokeParams.volumes = smokeVolumes;
            smokeParams.volumeCount = smokeVolumeCount;
            smokeParams.steps = 16.0f;
            smokeParams.composeOnly = true;
            m_volumeSmoke.Draw(smokeParams);
        }
    }

    if (bootstrap) {
        // First full fill: only mark the probe usable once every face succeeded,
        // otherwise retry the whole fill next frame.
        if (captureComplete) {
            slot.reflectionInitialized = true;
            slot.reflectionPendingFaces = 0;
            slot.reflectionSignature = signature;
            slot.reflectionSignatureValid = true;
        }
        slot.reflectionValid = captureComplete;
        return;
    }

    // Amortized path: the cubemap already holds valid (if slightly stale) faces,
    // so it stays sampled every frame while this slice refreshes.
    slot.reflectionFaceCursor =
        (slot.reflectionFaceCursor + faceRenderCount) % ReflectionCapture::kFaceCount;
    if (captureComplete) {
        slot.reflectionPendingFaces = slot.reflectionPendingFaces > faceRenderCount
            ? slot.reflectionPendingFaces - faceRenderCount
            : 0;
        if (slot.reflectionPendingFaces == 0) {
            slot.reflectionSignature = signature;
            slot.reflectionSignatureValid = true;
        }
    }
    slot.reflectionValid = true;
}

} // namespace Concord
