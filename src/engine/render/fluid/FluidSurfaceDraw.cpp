#include "engine/render/fluid/BgfxFluidRenderer.h"

#include "engine/debug/Logger.h"

#include <algorithm>
#include <cmath>

namespace {

float SrgbChannelToLinear(std::uint32_t rgba, int shift) noexcept
{
    const float c = static_cast<float>((rgba >> shift) & 0xffu) / 255.0f;
    return c <= 0.04045f ? c / 12.92f
        : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

void UnpackLinearRgb(std::uint32_t rgba, float out[3]) noexcept
{
    out[0] = SrgbChannelToLinear(rgba, 24);
    out[1] = SrgbChannelToLinear(rgba, 16);
    out[2] = SrgbChannelToLinear(rgba, 8);
}

const Concord::RenderLight* SunLight(const Concord::RenderLight* lights,
                                     std::uint32_t lightCount) noexcept
{
    if (lights == nullptr) {
        return nullptr;
    }
    const std::uint32_t count = std::min(lightCount, Concord::kMaxRenderLights);
    const Concord::RenderLight* directional = nullptr;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (lights[i].type != Concord::LightType::Directional) {
            continue;
        }
        directional = &lights[i];
        if (lights[i].sun) {
            break;
        }
    }
    return directional;
}

} // namespace

namespace Concord {

void BgfxFluidRenderer::Draw(const DrawParams& params,
                             const SkyEnvironment& environment,
                             const RenderFluid* fluids,
                             std::uint32_t fluidCount)
{
    if (params.view == kInvalidRenderView || params.sceneFb.idx == bgfx::kInvalidHandle
        || params.width == 0 || params.height == 0 || params.eye == nullptr
        || params.viewMatrix == nullptr || params.projectionMatrix == nullptr
        || fluids == nullptr || fluidCount == 0 || !EnsureReady()) {
        return;
    }

    bgfx::setViewFrameBuffer(params.view, params.sceneFb);
    bgfx::setViewRect(params.view, 0, 0,
                      static_cast<std::uint16_t>(params.width),
                      static_cast<std::uint16_t>(params.height));
    bgfx::setViewTransform(params.view, params.viewMatrix, params.projectionMatrix);
    bgfx::setViewMode(params.view, bgfx::ViewMode::Sequential);
    bgfx::touch(params.view);

    const bool refract = bgfx::isValid(params.sceneColor) && bgfx::isValid(params.sceneDepth)
        && bgfx::isValid(params.sceneColorCopy) && bgfx::isValid(params.sceneDepthCopy)
        && (bgfx::getCaps()->supported & BGFX_CAPS_TEXTURE_BLIT) != 0;
    if (refract) {
        bgfx::blit(params.view, params.sceneColorCopy, 0, 0, params.sceneColor);
        bgfx::blit(params.view, params.sceneDepthCopy, 0, 0, params.sceneDepth);
    }

    const RenderLight* sun = SunLight(params.lights, params.lightCount);
    float sunDir[4] = {0.0f, -1.0f, 0.0f, 0.0f};
    float sunColor[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    if (sun != nullptr) {
        sunDir[0] = sun->direction[0];
        sunDir[1] = sun->direction[1];
        sunDir[2] = sun->direction[2];
        sunDir[3] = std::max(sun->intensity, 0.0f);
        UnpackLinearRgb(sun->color, sunColor);
    }
    float zenith[4] = {};
    float horizon[4] = {};
    UnpackLinearRgb(environment.zenithColor, zenith);
    UnpackLinearRgb(environment.horizonColor, horizon);
    zenith[3] = std::max(environment.intensity, 0.0f);

    const float eyeParams[4] = {
        params.eye[0], params.eye[1], params.eye[2], std::max(params.nearPlane, 1.0e-3f)};
    const float screenParams[4] = {
        static_cast<float>(params.width), static_cast<float>(params.height),
        std::max(params.farPlane, params.nearPlane + 1.0e-3f),
        bgfx::getCaps()->originBottomLeft ? 0.0f : 1.0f};
    bgfx::setUniform(m_uEye, eyeParams);
    bgfx::setUniform(m_uScreen, screenParams);
    bgfx::setUniform(m_uSunDir, sunDir);
    bgfx::setUniform(m_uSunColor, sunColor);
    bgfx::setUniform(m_uSkyZenith, zenith);
    bgfx::setUniform(m_uSkyHorizon, horizon);

    const std::uint64_t stateBits = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
        | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_BLEND_ALPHA
        | BGFX_STATE_CULL_CCW;
    for (std::uint32_t i = 0; i < fluidCount; ++i) {
        const RenderFluid& fluid = fluids[i];
        const FluidKey key{params.ownerView, fluid.fluidKey};
        const auto it = m_bodies.find(key);
        if (it == m_bodies.end() || !it->second.valid) {
            continue;
        }
        const FluidGpuState& body = it->second;
        if (!bgfx::isValid(body.fieldTex[body.fieldRead]) || !bgfx::isValid(body.counterTex)
            || body.maxVerts == 0) {
            continue;
        }

        float color[4] = {};
        UnpackLinearRgb(fluid.waterColor, color);
        color[3] = std::max(fluid.fieldCell * 64.0f, 1.0f);
        const float optics[4] = {
            fluid.ior, fluid.absorption, fluid.roughness, fluid.isoLevel};
        const float field0[4] = {
            fluid.fieldOrigin[0], fluid.fieldOrigin[1], fluid.fieldOrigin[2], fluid.fieldCell};
        const float field1[4] = {
            static_cast<float>(fluid.fieldDims[0]), static_cast<float>(fluid.fieldDims[1]),
            static_cast<float>(fluid.fieldDims[2]),
            static_cast<float>(std::clamp<std::uint32_t>(
                static_cast<std::uint32_t>(std::ceil(std::sqrt(
                    static_cast<float>(fluid.fieldDims[0] * fluid.fieldDims[0]
                        + fluid.fieldDims[1] * fluid.fieldDims[1]
                        + fluid.fieldDims[2] * fluid.fieldDims[2])))) * 2u,
                12u, 32u))};
        bgfx::setUniform(m_uOptics, optics);
        bgfx::setUniform(m_uColor, color);
        bgfx::setUniform(m_uField0, field0);
        bgfx::setUniform(m_uField1, field1);
        bgfx::setUniform(m_uInvModel, fluid.worldInverse);
        bgfx::setUniform(m_uModel, fluid.world);
        if (refract) {
            bgfx::setTexture(0, m_sSceneColor, params.sceneColorCopy);
            bgfx::setTexture(1, m_sSceneDepth, params.sceneDepthCopy);
        }
        bgfx::setTexture(2, m_sField3d, body.fieldTex[body.fieldRead]);
        bgfx::setState(stateBits);
        bgfx::setTransform(fluid.world);
        // The vertex shader reads the live vertex count from counterTex and
        // collapses the unused tail of the stream, so no CPU read-back is needed.
        bgfx::setVertexBuffer(0, body.meshVerts, 0, body.maxVerts);
        bgfx::submit(params.view, m_drawProgram);
        if (!m_loggedDrawActive) {
            m_loggedDrawActive = true;
            Debug::Logger::Info("Render",
                                "DFSPH fluid draw active (particles=%u boundary=%u field=%ux%ux%u maxVerts=%u)",
                                fluid.particleCount, fluid.boundaryCount,
                                fluid.fieldDims[0], fluid.fieldDims[1], fluid.fieldDims[2],
                                body.maxVerts);
        }
    }
}

} // namespace Concord
