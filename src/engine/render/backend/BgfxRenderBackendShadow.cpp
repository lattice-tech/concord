#include "engine/render/backend/BgfxRenderBackend.h"

#include "engine/render/backend/BgfxSceneAabb.h"
#include "engine/render/shadow/ShadowFrustum.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {

namespace Detail = Concord::RenderDetail;
using Detail::FindShadowCaster;

void TransformPoint(float out[3], const float matrix[16], float x, float y, float z)
{
    out[0] = x * matrix[0] + y * matrix[4] + z * matrix[8] + matrix[12];
    out[1] = x * matrix[1] + y * matrix[5] + z * matrix[9] + matrix[13];
    out[2] = x * matrix[2] + y * matrix[6] + z * matrix[10] + matrix[14];
}

void BuildCascadeCorners(float out[8][3], const float inverseView[16],
                         Concord::Projection projection, float fovYDegrees,
                         float orthoHeight, float aspect, float nearDepth, float farDepth)
{
    constexpr float kPi = 3.14159265358979323846f;
    float nearHalfHeight = orthoHeight * 0.5f;
    float farHalfHeight = nearHalfHeight;
    if (projection == Concord::Projection::Perspective) {
        const float tangent = std::tan(fovYDegrees * kPi / 360.0f);
        nearHalfHeight = tangent * nearDepth;
        farHalfHeight = tangent * farDepth;
    }
    const float nearHalfWidth = nearHalfHeight * aspect;
    const float farHalfWidth = farHalfHeight * aspect;
    const float viewCorners[8][3] = {
        {-nearHalfWidth, -nearHalfHeight, nearDepth}, {nearHalfWidth, -nearHalfHeight, nearDepth},
        {-nearHalfWidth,  nearHalfHeight, nearDepth}, {nearHalfWidth,  nearHalfHeight, nearDepth},
        {-farHalfWidth, -farHalfHeight, farDepth}, {farHalfWidth, -farHalfHeight, farDepth},
        {-farHalfWidth,  farHalfHeight, farDepth}, {farHalfWidth,  farHalfHeight, farDepth},
    };
    for (int i = 0; i < 8; ++i) {
        TransformPoint(out[i], inverseView,
                       viewCorners[i][0], viewCorners[i][1], viewCorners[i][2]);
    }
}

} // namespace

namespace Concord {

void BgfxRenderBackend::RenderShadowPass(RenderViewHandle view, ViewSlot& slot,
                                         const float cameraView[16], Projection projection,
                                         float fovYDegrees, float orthoHeight, float aspect,
                                         float nearPlane, float farPlane,
                                         const RenderLight* lights, std::uint32_t lightCount,
                                         ShadowPassData& out)
{
    out = ShadowPassData{};

    const int caster = FindShadowCaster(lights, lightCount);
    const auto TouchCascades = [&slot] {
        for (RenderViewHandle shadowView : slot.shadowViews) {
            if (shadowView != kInvalidRenderView) {
                bgfx::touch(shadowView);
            }
        }
    };
    if (caster < 0 || !slot.shadowReady) {
        TouchCascades();
        return;
    }

    const auto pendingIt = m_pendingDraws.find(view);
    if (pendingIt == m_pendingDraws.end() || pendingIt->second.empty()) {
        TouchCascades();
        return;
    }

    const RenderLight& light = lights[caster];
    std::copy_n(light.direction, 3, out.lightDir);
    out.casterIndex = caster;

    const bool hasOpaqueCaster = std::any_of(
        pendingIt->second.begin(), pendingIt->second.end(), [](const MeshDrawCommand& command) {
            return command.material.blend == Material::BlendMode::Opaque;
        });
    if (!hasOpaqueCaster) {
        TouchCascades();
        return;
    }

    const float safeNear = std::max(nearPlane, 0.01f);
    const float safeFar = std::max(farPlane, safeNear + 0.01f);
    float previousSplit = safeNear;
    for (std::uint32_t cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
        const float ratio = static_cast<float>(cascade + 1) / static_cast<float>(kShadowCascadeCount);
        const float logarithmic = safeNear * std::pow(safeFar / safeNear, ratio);
        const float uniform = safeNear + (safeFar - safeNear) * ratio;
        out.splitDepths[cascade] = uniform
            + (logarithmic - uniform) * m_shadowConfig.cascadeSplitLambda;
        const float interval = out.splitDepths[cascade] - previousSplit;
        out.blendWidths[cascade] = interval * m_shadowConfig.cascadeBlendFraction;
        previousSplit = out.splitDepths[cascade];
    }

    float inverseView[16];
    bx::mtxInverse(inverseView, cameraView);
    std::memcpy(out.cameraView, cameraView, sizeof(out.cameraView));
    std::array<ShadowFrustumResult, kShadowCascadeCount> frusta{};
    constexpr float kPi = 3.14159265358979323846f;
    const float angularRadiusDegrees = std::isfinite(light.directionalAngularRadiusDegrees)
        ? std::clamp(light.directionalAngularRadiusDegrees, 0.0f, 45.0f)
        : kDefaultDirectionalAngularRadiusDegrees;
    const float angularRadius = angularRadiusDegrees * kPi / 180.0f;
    // Reserve enough map border for the widest PCSS search/filter, one normal-
    // bias texel and center snapping. This prevents CLAMP_TO_EDGE from turning
    // an out-of-range kernel into a repeated dark/bright border.
    const float samplingGuardTexels = std::max(m_shadowConfig.blockerSearchRadiusTexels,
                                               m_shadowConfig.maxFilterRadiusTexels)
        + m_shadowConfig.normalBiasTexels + 1.0f;
    for (std::uint32_t cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
        // The next cascade must include the full cross-fade interval sampled
        // before the previous split, rather than starting only at the split.
        const float cascadeNear = cascade == 0
            ? safeNear
            : std::max(safeNear, out.splitDepths[cascade - 1] - out.blendWidths[cascade - 1]);
        float receiverCorners[8][3];
        BuildCascadeCorners(receiverCorners, inverseView, projection, fovYDegrees,
                            orthoHeight, aspect, cascadeNear, out.splitDepths[cascade]);
        ComputeCascadeShadowFrustum(light.direction, receiverCorners,
                                    m_shadowConfig.casterExtrusionWorld,
                                    m_shadowConfig.resolution,
                                    samplingGuardTexels,
                                    bgfx::getCaps()->homogeneousDepth, frusta[cascade]);
        std::memcpy(out.lightViewProj[cascade].data(), frusta[cascade].viewProjMatrix,
                    sizeof(frusta[cascade].viewProjMatrix));
        const float worldUnitsPerTexel = frusta[cascade].orthoWidth
            / static_cast<float>(m_shadowConfig.resolution);
        out.normalBiasWorld[cascade] = m_shadowConfig.normalBiasTexels * worldUnitsPerTexel;
        out.penumbraScaleTexels[cascade] = frusta[cascade].depthRange * std::tan(angularRadius)
            / std::max(worldUnitsPerTexel, 1e-6f);
    }
    out.valid = true;

    m_batcher.BeginFrame();
    m_skinnedScratch.clear();
    for (const MeshDrawCommand& command : pendingIt->second) {
        if (command.material.blend != Material::BlendMode::Opaque) {
            continue;
        }
        if (command.bonePalette != nullptr && command.boneCount > 0) {
            m_skinnedScratch.push_back(&command);
        } else {
            m_batcher.Add(command);
        }
    }
    m_batcher.Finish();

    constexpr std::uint16_t kInstanceStride = sizeof(float) * 16;
    const std::uint64_t shadowState = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
        | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;

    for (std::uint32_t cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
        const RenderViewHandle shadowView = slot.shadowViews[cascade];
        m_shadowMap.BeginDepthPass(shadowView, slot.shadowFbs[cascade], m_shadowConfig,
                                   frusta[cascade].viewMatrix, frusta[cascade].projMatrix);
        bool submitted = false;
        for (const RenderBatch& batch : m_batcher.Batches()) {
            const BgfxMeshStore::BgfxMesh* mesh = m_meshes.Get(batch.mesh);
            if (mesh == nullptr) {
                continue;
            }
            const auto requested = static_cast<std::uint32_t>(batch.commands.size());
            const std::uint32_t count = std::min(
                requested, bgfx::getAvailInstanceDataBuffer(requested, kInstanceStride));
            if (count == 0) {
                continue;
            }
            bgfx::InstanceDataBuffer idb;
            bgfx::allocInstanceDataBuffer(&idb, count, kInstanceStride);
            std::uint8_t* cursor = idb.data;
            for (std::uint32_t index = 0; index < count; ++index) {
                std::memcpy(cursor, batch.commands[index]->worldMatrix, kInstanceStride);
                cursor += kInstanceStride;
            }
            bgfx::setState(shadowState);
            bgfx::setVertexBuffer(0, mesh->vb);
            bgfx::setIndexBuffer(mesh->ib);
            float identity[16];
            bx::mtxIdentity(identity);
            bgfx::setTransform(identity);
            bgfx::setInstanceDataBuffer(&idb);
            bgfx::submit(shadowView, m_shadowMap.Program());
            submitted = true;
        }
        for (const MeshDrawCommand* command : m_skinnedScratch) {
            const BgfxMeshStore::BgfxMesh* mesh = m_meshes.Get(command->mesh);
            if (mesh == nullptr || !mesh->skinned
                || bgfx::getAvailInstanceDataBuffer(1, kInstanceStride) < 1) {
                continue;
            }
            m_uniforms.BindBones(command->bonePalette, command->boneCount);
            bgfx::InstanceDataBuffer idb;
            bgfx::allocInstanceDataBuffer(&idb, 1, kInstanceStride);
            std::memcpy(idb.data, command->worldMatrix, kInstanceStride);
            bgfx::setState(shadowState);
            bgfx::setVertexBuffer(0, mesh->vb);
            bgfx::setIndexBuffer(mesh->ib);
            float identity[16];
            bx::mtxIdentity(identity);
            bgfx::setTransform(identity);
            bgfx::setInstanceDataBuffer(&idb);
            bgfx::submit(shadowView, m_shadowMap.SkinnedProgram());
            submitted = true;
        }
        if (!submitted) {
            bgfx::touch(shadowView);
        }
    }
}

} // namespace Concord
