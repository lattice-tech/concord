#include "engine/render/backend/BgfxSceneUniforms.h"

#include "engine/debug/Logger.h"
#include "engine/render/backend/BgfxMathConverters.h"
#include "engine/render/frame/SkyEnvironment.h"
#include "engine/render/texture/BgfxTextureCache.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using Concord::RenderDetail::ColorToFloat4;

constexpr std::uint32_t kClusterIndexWidth = 1024;
constexpr std::uint32_t kClusterIndexRows =
    (Concord::ClusterGrid::kClusterCount * Concord::ClusterGrid::kMaxLightsPerCluster
     + kClusterIndexWidth - 1) / kClusterIndexWidth;

float SmoothStep(float edge0, float edge1, float value) noexcept
{
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float SkyDaylightFactor(const Concord::RenderLight* lights,
                        std::uint32_t lightCount) noexcept
{
    if (lights == nullptr) {
        return 1.0f;
    }
    for (std::uint32_t index = 0; index < lightCount; ++index) {
        if (lights[index].type == Concord::LightType::Directional) {
            const float sunHeight = -lights[index].direction[1];
            return SmoothStep(-0.10f, 0.04f, sunHeight);
        }
    }
    return 1.0f;
}

} // namespace

namespace Concord {

void BgfxSceneUniforms::EnsureReady()
{
    if (m_initialized) {
        return;
    }
    m_uAlbedo = bgfx::createUniform("u_albedo", bgfx::UniformType::Vec4);
    m_uGradientTo = bgfx::createUniform("u_gradientTo", bgfx::UniformType::Vec4);
    m_uEmissive = bgfx::createUniform("u_emissive", bgfx::UniformType::Vec4);
    m_uMatParams = bgfx::createUniform("u_matParams", bgfx::UniformType::Vec4);
    m_uLightPosType = bgfx::createUniform("u_lightPosType", bgfx::UniformType::Vec4, kMaxRenderLights);
    m_uLightDirRange = bgfx::createUniform("u_lightDirRange", bgfx::UniformType::Vec4, kMaxRenderLights);
    m_uLightColor = bgfx::createUniform("u_lightColor", bgfx::UniformType::Vec4, kMaxRenderLights);
    m_uLightSpot = bgfx::createUniform("u_lightSpot", bgfx::UniformType::Vec4, kMaxRenderLights);
    m_uAmbient = bgfx::createUniform("u_ambient", bgfx::UniformType::Vec4);
    m_uCamPos = bgfx::createUniform("u_camPos", bgfx::UniformType::Vec4);
    m_sAlbedo = bgfx::createUniform("s_albedo", bgfx::UniformType::Sampler);
    m_sNormal = bgfx::createUniform("s_normal", bgfx::UniformType::Sampler);
    m_sMetallicRoughness = bgfx::createUniform("s_metallicRoughness", bgfx::UniformType::Sampler);
    m_sEmissive = bgfx::createUniform("s_emissive", bgfx::UniformType::Sampler);
    m_uTexFlags = bgfx::createUniform("u_texFlags", bgfx::UniformType::Vec4);
    m_uOutputFlags = bgfx::createUniform("u_outputFlags", bgfx::UniformType::Vec4);
    m_uBones = bgfx::createUniform("u_bones", bgfx::UniformType::Mat4, kMaxRenderBones);
    m_sPlanarReflection = bgfx::createUniform("s_planarReflection", bgfx::UniformType::Sampler);
    m_uPlanarViewProj = bgfx::createUniform("u_planarViewProj", bgfx::UniformType::Mat4);
    m_sSceneReflection = bgfx::createUniform("s_sceneReflection", bgfx::UniformType::Sampler);
    m_uReflectionFlags = bgfx::createUniform("u_reflectionFlags", bgfx::UniformType::Vec4);
    m_uReflectionProbe = bgfx::createUniform("u_reflectionProbe", bgfx::UniformType::Vec4);
    m_uReflectionBoxMin = bgfx::createUniform("u_reflectionBoxMin", bgfx::UniformType::Vec4);
    m_uReflectionBoxMax = bgfx::createUniform("u_reflectionBoxMax", bgfx::UniformType::Vec4);
    m_uClipPlane = bgfx::createUniform("u_clipPlane", bgfx::UniformType::Vec4);

    // Sampler handles are shared; ForwardPlusContext isolates texture storage
    // for each camera that can submit mesh shading in the same frame.
    m_sLightData = bgfx::createUniform("s_lightData", bgfx::UniformType::Sampler);
    m_sClusterRanges = bgfx::createUniform("s_clusterRanges", bgfx::UniformType::Sampler);
    m_sLightIndices = bgfx::createUniform("s_lightIndices", bgfx::UniformType::Sampler);
    m_uClusterParams = bgfx::createUniform("u_clusterParams", bgfx::UniformType::Vec4);
    const bool fallbackReady = CreateForwardPlusTextures(m_fallbackClusters);
    m_forwardPlusReady = bgfx::isValid(m_sLightData)
        && bgfx::isValid(m_sClusterRanges)
        && bgfx::isValid(m_sLightIndices)
        && bgfx::isValid(m_uClusterParams)
        && fallbackReady;
    if (!m_forwardPlusReady) {
        Debug::Logger::Error(
            "Render", "Forward+ shared resource initialization failed; using fixed lights");
    }
    DisableClusters();

    std::array<std::uint8_t, 6 * 4> fallbackPixels{};
    for (std::size_t face = 0; face < 6; ++face) {
        fallbackPixels[face * 4 + 3] = 255;
    }
    constexpr std::uint64_t kCubeFlags = BGFX_SAMPLER_U_CLAMP
        | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;
    m_fallbackReflectionCube = bgfx::createTextureCube(
        1, false, 1, bgfx::TextureFormat::RGBA8, kCubeFlags,
        bgfx::copy(fallbackPixels.data(), static_cast<std::uint32_t>(fallbackPixels.size())));
    if (!bgfx::isValid(m_fallbackReflectionCube)) {
        Debug::Logger::Error("Render", "reflection fallback cubemap creation failed");
    }
    m_initialized = true;
}

void BgfxSceneUniforms::Shutdown()
{
    for (bgfx::UniformHandle* u : {&m_uAlbedo, &m_uGradientTo, &m_uEmissive, &m_uMatParams,
                                   &m_uLightPosType, &m_uLightDirRange, &m_uLightColor, &m_uLightSpot,
                                   &m_uAmbient, &m_uCamPos,
                                   &m_sAlbedo, &m_sNormal, &m_sMetallicRoughness, &m_sEmissive,
                                   &m_uTexFlags, &m_uOutputFlags,
                                   &m_uBones, &m_sPlanarReflection, &m_uPlanarViewProj,
                                   &m_sSceneReflection, &m_uReflectionFlags,
                                    &m_uReflectionProbe, &m_uReflectionBoxMin,
                                    &m_uReflectionBoxMax, &m_uClipPlane,
                                    &m_sLightData, &m_sClusterRanges, &m_sLightIndices,
                                    &m_uClusterParams}) {
        if (bgfx::isValid(*u)) {
            bgfx::destroy(*u);
            *u = BGFX_INVALID_HANDLE;
        }
    }
    DestroyForwardPlusContext(m_fallbackClusters);
    m_activeLightData = BGFX_INVALID_HANDLE;
    m_activeClusterRanges = BGFX_INVALID_HANDLE;
    m_activeLightIndices = BGFX_INVALID_HANDLE;
    m_forwardPlusReady = false;
    if (bgfx::isValid(m_fallbackReflectionCube)) {
        bgfx::destroy(m_fallbackReflectionCube);
        m_fallbackReflectionCube = BGFX_INVALID_HANDLE;
    }
    m_initialized = false;
}

void BgfxSceneUniforms::BindBones(const float* palette, std::uint32_t count)
{
    if (!m_initialized || palette == nullptr || count == 0) {
        return;
    }
    if (count > kMaxRenderBones) {
        count = kMaxRenderBones;
    }
    bgfx::setUniform(m_uBones, palette, count);
}

bool BgfxSceneUniforms::CreateForwardPlusContext(ForwardPlusContext& context) const
{
    if (!ForwardPlusReady()) {
        return false;
    }
    return CreateForwardPlusTextures(context);
}

bool BgfxSceneUniforms::CreateForwardPlusTextures(ForwardPlusContext& context) const
{
    if (context.Valid()) {
        return true;
    }
    DestroyForwardPlusContext(context);

    constexpr std::uint64_t kSampleFlags = BGFX_SAMPLER_POINT
        | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    constexpr std::uint64_t kComputeFlags = kSampleFlags | BGFX_TEXTURE_COMPUTE_WRITE;
    context.lightData = bgfx::createTexture2D(
        4, static_cast<std::uint16_t>(ClusterGrid::kMaxPackedLights), false, 1,
        bgfx::TextureFormat::RGBA32F, kSampleFlags);
    context.clusterRanges = bgfx::createTexture2D(
        static_cast<std::uint16_t>(ClusterGrid::kDimX * ClusterGrid::kDimY),
        static_cast<std::uint16_t>(ClusterGrid::kDimZ), false, 1,
        bgfx::TextureFormat::RGBA32F, kComputeFlags);
    context.lightIndices = bgfx::createTexture2D(
        static_cast<std::uint16_t>(kClusterIndexWidth),
        static_cast<std::uint16_t>(kClusterIndexRows), false, 1,
        bgfx::TextureFormat::R32F, kComputeFlags);
    if (!context.Valid()) {
        DestroyForwardPlusContext(context);
        return false;
    }
    return true;
}

void BgfxSceneUniforms::DestroyForwardPlusContext(ForwardPlusContext& context) const
{
    for (bgfx::TextureHandle* texture : {
             &context.lightData, &context.clusterRanges, &context.lightIndices}) {
        if (bgfx::isValid(*texture)) {
            bgfx::destroy(*texture);
            *texture = BGFX_INVALID_HANDLE;
        }
    }
    context.params[0] = 0.0f;
    context.params[1] = 0.0f;
    context.params[2] = 0.1f;
    context.params[3] = 200.0f;
}

bool BgfxSceneUniforms::PrepareClustersGpu(ForwardPlusContext& context,
                                           const ClusteredLightCuller& culler,
                                           const ClusterGrid& grid) const
{
    if (!ForwardPlusReady() || !context.Valid()) {
        return false;
    }
    const std::vector<GpuLight>& lights = culler.Lights();
    const std::uint32_t lightRows =
        std::min<std::uint32_t>(static_cast<std::uint32_t>(lights.size()),
                                ClusterGrid::kMaxPackedLights);
    if (lightRows > 0) {
        bgfx::updateTexture2D(context.lightData, 0, 0, 0, 0, 4,
                              static_cast<std::uint16_t>(lightRows),
                              bgfx::copy(lights.data(), lightRows
                                  * static_cast<std::uint32_t>(sizeof(GpuLight))));
    }
    context.params[0] = 1.0f;
    context.params[1] = static_cast<float>(culler.DirectionalCount());
    context.params[2] = grid.nearPlane;
    context.params[3] = grid.farPlane;
    return true;
}

bool BgfxSceneUniforms::UpdateClustersCpu(ForwardPlusContext& context,
                                          const ClusteredLightCuller& culler,
                                          const ClusterGrid& grid) const
{
    if (!PrepareClustersGpu(context, culler, grid)) {
        return false;
    }

    const std::uint32_t rangeWidth = ClusterGrid::kDimX * ClusterGrid::kDimY;
    const std::uint32_t rangeHeight = ClusterGrid::kDimZ;
    std::vector<float> rangeData(
        static_cast<std::size_t>(rangeWidth) * rangeHeight * 4, 0.0f);
    const std::vector<ClusteredLightCuller::ClusterRange>& ranges = culler.Ranges();
    const std::size_t rangeCount = std::min(
        ranges.size(), static_cast<std::size_t>(rangeWidth) * rangeHeight);
    for (std::size_t index = 0; index < rangeCount; ++index) {
        rangeData[index * 4] = static_cast<float>(ranges[index].offset);
        rangeData[index * 4 + 1] = static_cast<float>(ranges[index].count);
    }
    bgfx::updateTexture2D(
        context.clusterRanges, 0, 0, 0, 0,
        static_cast<std::uint16_t>(rangeWidth),
        static_cast<std::uint16_t>(rangeHeight),
        bgfx::copy(rangeData.data(),
                   static_cast<std::uint32_t>(rangeData.size() * sizeof(float))));

    const std::vector<std::uint32_t>& indices = culler.Indices();
    if (!indices.empty()) {
        std::uint32_t rows = (static_cast<std::uint32_t>(indices.size())
                              + kClusterIndexWidth - 1) / kClusterIndexWidth;
        rows = std::min(rows, kClusterIndexRows);
        std::vector<float> indexData(
            static_cast<std::size_t>(kClusterIndexWidth) * rows, 0.0f);
        const std::size_t indexCount = std::min(indices.size(), indexData.size());
        for (std::size_t index = 0; index < indexCount; ++index) {
            indexData[index] = static_cast<float>(indices[index]);
        }
        bgfx::updateTexture2D(
            context.lightIndices, 0, 0, 0, 0,
            static_cast<std::uint16_t>(kClusterIndexWidth),
            static_cast<std::uint16_t>(rows),
            bgfx::copy(indexData.data(),
                       static_cast<std::uint32_t>(indexData.size() * sizeof(float))));
    }
    return true;
}

void BgfxSceneUniforms::SelectClusters(const ForwardPlusContext& context)
{
    if (!ForwardPlusReady() || !context.Valid()) {
        DisableClusters();
        return;
    }
    m_activeLightData = context.lightData;
    m_activeClusterRanges = context.clusterRanges;
    m_activeLightIndices = context.lightIndices;
    std::copy_n(context.params, 4, m_clusterParams);
}

void BgfxSceneUniforms::DisableClusters()
{
    m_activeLightData = BGFX_INVALID_HANDLE;
    m_activeClusterRanges = BGFX_INVALID_HANDLE;
    m_activeLightIndices = BGFX_INVALID_HANDLE;
    if (ForwardPlusReady()) {
        m_activeLightData = m_fallbackClusters.lightData;
        m_activeClusterRanges = m_fallbackClusters.clusterRanges;
        m_activeLightIndices = m_fallbackClusters.lightIndices;
    }
    m_clusterParams[0] = 0.0f;
    m_clusterParams[1] = 0.0f;
    m_clusterParams[2] = 0.1f;
    m_clusterParams[3] = 200.0f;
}

void BgfxSceneUniforms::ApplyLighting(const CameraView* camera, const RenderLight* lights,
                                      std::uint32_t lightCount, const SkyEnvironment* sky)
{
    // The legacy fixed-light arrays are relevant only when clustered resources
    // are unavailable. Forward+ draws get their full packed list from textures.
    const std::uint32_t inputLightCount = lights != nullptr ? lightCount : 0;
    const bool classicLighting = m_clusterParams[0] <= 0.5f;
    const std::uint32_t fixedLightCount = classicLighting
        ? std::min(inputLightCount, kMaxRenderLights) : 0;
    if (classicLighting && inputLightCount > kMaxRenderLights) {
        // ApplyLighting runs once per batch/pass (many times per frame), so this
        // diagnostic must be throttled — logging it unconditionally serializes
        // every draw behind a mutex + fflush(stderr) and tanks frame time.
        static std::uint32_t s_lastLoggedCount = 0;
        if (inputLightCount != s_lastLoggedCount) {
            s_lastLoggedCount = inputLightCount;
            Debug::Logger::Debug("Render", "%u lights exceeds the %u-light cap; rendering the first %u",
                                 inputLightCount, kMaxRenderLights, kMaxRenderLights);
        }
    }

    if (classicLighting) {
        float posType[kMaxRenderLights][4] = {};
        float dirRange[kMaxRenderLights][4] = {};
        float color[kMaxRenderLights][4] = {};
        float spot[kMaxRenderLights][4] = {};
        for (std::uint32_t index = 0; index < fixedLightCount; ++index) {
            const RenderLight& light = lights[index];
            posType[index][0] = light.position[0];
            posType[index][1] = light.position[1];
            posType[index][2] = light.position[2];
            posType[index][3] = static_cast<float>(light.type);

            dirRange[index][0] = light.direction[0];
            dirRange[index][1] = light.direction[1];
            dirRange[index][2] = light.direction[2];
            dirRange[index][3] = light.range > 0.0f ? light.range : 1.0f;

            float rgb[4];
            ColorToFloat4(rgb, light.color);
            color[index][0] = rgb[0];
            color[index][1] = rgb[1];
            color[index][2] = rgb[2];
            color[index][3] = light.intensity;

            spot[index][0] = std::cos(
                light.innerAngleDegrees * 3.14159265358979323846f / 180.0f);
            spot[index][1] = std::cos(
                light.outerAngleDegrees * 3.14159265358979323846f / 180.0f);
            spot[index][2] = light.sourceRadius;
            spot[index][3] = 0.0f;
        }

        // Upload the full arrays so an empty scene replaces fixed-light values
        // retained from a previous classic draw.
        bgfx::setUniform(m_uLightPosType, posType, kMaxRenderLights);
        bgfx::setUniform(m_uLightDirRange, dirRange, kMaxRenderLights);
        bgfx::setUniform(m_uLightColor, color, kMaxRenderLights);
        bgfx::setUniform(m_uLightSpot, spot, kMaxRenderLights);
    }

    // Indirect fill is the scene sky colour (see SkyEnvironment) at ambient
    // intensity; the w slot carries the active light count for the shader.
    float ambient[4] = {0.0f, 0.0f, 0.0f, static_cast<float>(fixedLightCount)};
    if (sky != nullptr) {
        float ambientIntensity = sky->ambientIntensity;
        if (sky->mode == SkyMode::Procedural) {
            const float daylight = SkyDaylightFactor(lights, inputLightCount);
            ambientIntensity = sky->nightAmbientIntensity
                + (sky->ambientIntensity - sky->nightAmbientIntensity) * daylight;
        }
        SkyAmbientColor(*sky, ambientIntensity, ambient);
    } else {
        SkyAmbientColor(ambient);
    }
    ambient[3] = static_cast<float>(fixedLightCount);
    bgfx::setUniform(m_uAmbient, ambient);

    const float camPos[4] = {
        camera ? camera->eye[0] : 0.0f,
        camera ? camera->eye[1] : 0.0f,
        camera ? camera->eye[2] : 0.0f,
        1.0f,
    };
    bgfx::setUniform(m_uCamPos, camPos);
}

void BgfxSceneUniforms::ApplyMaterial(const RenderMaterial& material, BgfxTextureCache& textures, bool flipV,
                                      bgfx::TextureHandle planarReflection,
                                      const float planarViewProj[16], bool linearOutput,
                                      bgfx::TextureHandle sceneReflection,
                                      const float reflectionProbe[3],
                                       const float reflectionBoxMin[3],
                                       const float reflectionBoxMax[3],
                                       bool realtimeReflection,
                                       const float clipPlane[4])
{
    float albedo[4];
    ColorToFloat4(albedo, material.albedo);
    bgfx::setUniform(m_uAlbedo, albedo);

    float gradientTo[4];
    ColorToFloat4(gradientTo, material.gradientTo);
    bgfx::setUniform(m_uGradientTo, gradientTo);

    float emissive[4];
    ColorToFloat4(emissive, material.emissive);
    emissive[3] = material.emissiveStrength; // pack strength into alpha slot
    bgfx::setUniform(m_uEmissive, emissive);

    const float gradientCode = material.gradient ? static_cast<float>(material.gradientAxis + 1) : 0.0f;
    const float params[4] = {
        material.metallic,
        material.roughness,
        material.lit ? 1.0f : 0.0f,
        gradientCode,
    };
    bgfx::setUniform(m_uMatParams, params);

    constexpr std::uint64_t kMatSampler = BGFX_SAMPLER_U_CLAMP
        | BGFX_SAMPLER_V_CLAMP
        | BGFX_SAMPLER_W_CLAMP
        | BGFX_SAMPLER_MIN_ANISOTROPIC
        | BGFX_SAMPLER_MAG_ANISOTROPIC;
    const bgfx::TextureHandle white = textures.White();
    const bgfx::TextureHandle albedoTex = textures.Get(material.albedoMap);
    const bgfx::TextureHandle normalTex = textures.Get(material.normalMap);
    const bgfx::TextureHandle mrTex = textures.Get(material.metallicRoughnessMap);
    const bgfx::TextureHandle emissiveTex = textures.Get(material.emissiveMap);

    bgfx::setTexture(0, m_sAlbedo,            bgfx::isValid(albedoTex) ? albedoTex : white, kMatSampler);
    bgfx::setTexture(1, m_sNormal,            bgfx::isValid(normalTex) ? normalTex : white, kMatSampler);
    bgfx::setTexture(2, m_sMetallicRoughness, bgfx::isValid(mrTex) ? mrTex : white, kMatSampler);
    bgfx::setTexture(3, m_sEmissive,          bgfx::isValid(emissiveTex) ? emissiveTex : white, kMatSampler);

    const bool usePlanar = material.planarReflection && bgfx::isValid(planarReflection)
        && planarViewProj != nullptr;
    bgfx::setTexture(7, m_sPlanarReflection, usePlanar ? planarReflection : white, kMatSampler);
    if (usePlanar) {
        bgfx::setUniform(m_uPlanarViewProj, planarViewProj);
    } else {
        float identity[16];
        bx::mtxIdentity(identity);
        bgfx::setUniform(m_uPlanarViewProj, identity);
    }

    // x: normal map; y: shadow V flip; z: blend mode; w: planar reflection.
    const float texFlags[4] = {
        bgfx::isValid(normalTex) ? 1.0f : 0.0f,
        flipV ? 1.0f : 0.0f,
        static_cast<float>(material.blend),
        usePlanar ? 1.0f : 0.0f,
    };
    bgfx::setUniform(m_uTexFlags, texFlags);

    const float outputFlags[4] = {linearOutput ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(m_uOutputFlags, outputFlags);

    const bool useReflection = realtimeReflection && bgfx::isValid(sceneReflection);
    const bgfx::TextureHandle reflectionTexture = bgfx::isValid(sceneReflection)
        ? sceneReflection : m_fallbackReflectionCube;
    bgfx::setTexture(8, m_sSceneReflection, reflectionTexture, kMatSampler);
    const float reflectionFlags[4] = {
        useReflection ? 1.0f : 0.0f,
        std::clamp(material.reflectivity, 0.0f, 1.0f),
        0.0f,
        0.0f,
    };
    bgfx::setUniform(m_uReflectionFlags, reflectionFlags);
    const bool useBoxProjection = useReflection
        && reflectionProbe != nullptr
        && reflectionBoxMin != nullptr
        && reflectionBoxMax != nullptr;
    const float probe[4] = {
        reflectionProbe != nullptr ? reflectionProbe[0] : 0.0f,
        reflectionProbe != nullptr ? reflectionProbe[1] : 0.0f,
        reflectionProbe != nullptr ? reflectionProbe[2] : 0.0f,
        0.0f,
    };
    const float boxMin[4] = {
        reflectionBoxMin != nullptr ? reflectionBoxMin[0] : 0.0f,
        reflectionBoxMin != nullptr ? reflectionBoxMin[1] : 0.0f,
        reflectionBoxMin != nullptr ? reflectionBoxMin[2] : 0.0f,
        useBoxProjection ? 1.0f : 0.0f,
    };
    const float boxMax[4] = {
        reflectionBoxMax != nullptr ? reflectionBoxMax[0] : 0.0f,
        reflectionBoxMax != nullptr ? reflectionBoxMax[1] : 0.0f,
        reflectionBoxMax != nullptr ? reflectionBoxMax[2] : 0.0f,
        0.0f,
    };
    bgfx::setUniform(m_uReflectionProbe, probe);
    bgfx::setUniform(m_uReflectionBoxMin, boxMin);
    bgfx::setUniform(m_uReflectionBoxMax, boxMax);
    const float disabledClip[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    bgfx::setUniform(m_uClipPlane, clipPlane != nullptr ? clipPlane : disabledClip);

    if (bgfx::isValid(m_uClusterParams)) {
        bgfx::setUniform(m_uClusterParams, m_clusterParams);
    }
    if (ForwardPlusReady()
        && bgfx::isValid(m_activeLightData)
        && bgfx::isValid(m_activeClusterRanges)
        && bgfx::isValid(m_activeLightIndices)) {
        bgfx::setTexture(9, m_sLightData, m_activeLightData);
        bgfx::setTexture(10, m_sClusterRanges, m_activeClusterRanges);
        bgfx::setTexture(11, m_sLightIndices, m_activeLightIndices);
    }
}

} // namespace Concord
