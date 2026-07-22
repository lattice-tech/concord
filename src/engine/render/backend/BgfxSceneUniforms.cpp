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
    const std::uint32_t count = std::min(lightCount, Concord::kMaxRenderLights);
    for (std::uint32_t index = 0; index < count; ++index) {
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

    // Forward+ clustered lighting: data textures (point-sampled, texelFetch) +
    // params. Created empty and updated per frame by UpdateClusters.
    m_sLightData = bgfx::createUniform("s_lightData", bgfx::UniformType::Sampler);
    m_sClusterRanges = bgfx::createUniform("s_clusterRanges", bgfx::UniformType::Sampler);
    m_sLightIndices = bgfx::createUniform("s_lightIndices", bgfx::UniformType::Sampler);
    m_uClusterParams = bgfx::createUniform("u_clusterParams", bgfx::UniformType::Vec4);
    constexpr std::uint64_t kDataTexFlags = BGFX_SAMPLER_POINT
        | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    m_lightDataTex = bgfx::createTexture2D(
        4, 256, false, 1, bgfx::TextureFormat::RGBA32F, kDataTexFlags);
    m_clusterRangeTex = bgfx::createTexture2D(
        static_cast<std::uint16_t>(ClusterGrid::kDimX * ClusterGrid::kDimY),
        static_cast<std::uint16_t>(ClusterGrid::kDimZ), false, 1,
        bgfx::TextureFormat::RGBA32F, kDataTexFlags);
    m_lightIndexTex = bgfx::createTexture2D(
        1024, 256, false, 1, bgfx::TextureFormat::R32F, kDataTexFlags);

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
    for (bgfx::TextureHandle* t : {&m_lightDataTex, &m_clusterRangeTex, &m_lightIndexTex}) {
        if (bgfx::isValid(*t)) {
            bgfx::destroy(*t);
            *t = BGFX_INVALID_HANDLE;
        }
    }
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

void BgfxSceneUniforms::UpdateClusters(const ClusteredLightCuller& culler,
                                      const ClusterGrid& grid, bool enabled)
{
    if (!m_initialized) {
        return;
    }
    if (!enabled) {
        m_clusterParams[0] = 0.0f; // classic 8-light path
        return;
    }

    // Light data texture: 4 texels/light; GpuLight is exactly 16 floats laid out
    // in texel order (posType, dirRange, colorLinear+intensity, spot).
    const std::vector<GpuLight>& lights = culler.Lights();
    const std::uint32_t lightRows =
        std::min<std::uint32_t>(static_cast<std::uint32_t>(lights.size()), 256u);
    if (lightRows > 0) {
        bgfx::updateTexture2D(m_lightDataTex, 0, 0, 0, 0, 4,
                              static_cast<std::uint16_t>(lightRows),
                              bgfx::copy(lights.data(), lightRows * static_cast<std::uint32_t>(sizeof(GpuLight))));
    }

    // Cluster range texture: (offset,count) per cluster in R,G.
    const std::uint32_t rw = ClusterGrid::kDimX * ClusterGrid::kDimY;
    const std::uint32_t rh = ClusterGrid::kDimZ;
    std::vector<float> rangeBuf(static_cast<std::size_t>(rw) * rh * 4, 0.0f);
    const std::vector<ClusteredLightCuller::ClusterRange>& ranges = culler.Ranges();
    const std::size_t rangeMax = std::min<std::size_t>(ranges.size(), static_cast<std::size_t>(rw) * rh);
    for (std::size_t c = 0; c < rangeMax; ++c) {
        rangeBuf[c * 4 + 0] = static_cast<float>(ranges[c].offset);
        rangeBuf[c * 4 + 1] = static_cast<float>(ranges[c].count);
    }
    bgfx::updateTexture2D(m_clusterRangeTex, 0, 0, 0, 0,
                          static_cast<std::uint16_t>(rw), static_cast<std::uint16_t>(rh),
                          bgfx::copy(rangeBuf.data(), static_cast<std::uint32_t>(rangeBuf.size() * sizeof(float))));

    // Light index texture: flat R32F list.
    const std::vector<std::uint32_t>& indices = culler.Indices();
    constexpr std::uint32_t kIndexWidth = 1024;
    if (!indices.empty()) {
        std::uint32_t rows = (static_cast<std::uint32_t>(indices.size()) + kIndexWidth - 1) / kIndexWidth;
        rows = std::min<std::uint32_t>(rows, 256u);
        std::vector<float> idxBuf(static_cast<std::size_t>(kIndexWidth) * rows, 0.0f);
        const std::size_t idxMax = std::min<std::size_t>(indices.size(), idxBuf.size());
        for (std::size_t k = 0; k < idxMax; ++k) {
            idxBuf[k] = static_cast<float>(indices[k]);
        }
        bgfx::updateTexture2D(m_lightIndexTex, 0, 0, 0, 0, static_cast<std::uint16_t>(kIndexWidth),
                              static_cast<std::uint16_t>(rows),
                              bgfx::copy(idxBuf.data(), static_cast<std::uint32_t>(idxBuf.size() * sizeof(float))));
    }

    m_clusterParams[0] = 1.0f;
    m_clusterParams[1] = static_cast<float>(culler.DirectionalCount());
    m_clusterParams[2] = grid.nearPlane;
    m_clusterParams[3] = grid.farPlane;
    m_activeRangeTex = m_clusterRangeTex;
    m_activeIndexTex = m_lightIndexTex;
}

void BgfxSceneUniforms::UpdateClustersGpu(const ClusteredLightCuller& culler, const ClusterGrid& grid,
                                          bgfx::TextureHandle rangeTex, bgfx::TextureHandle indexTex)
{
    if (!m_initialized) {
        return;
    }
    if (!bgfx::isValid(rangeTex) || !bgfx::isValid(indexTex)) {
        // GPU path unavailable this frame; caller should have already filled
        // the CPU textures via UpdateClusters as the fallback.
        return;
    }

    // Only the light-data texture needs a CPU upload here — the compute shader
    // reads it and produces the range/index textures itself.
    const std::vector<GpuLight>& lights = culler.Lights();
    const std::uint32_t lightRows =
        std::min<std::uint32_t>(static_cast<std::uint32_t>(lights.size()), 256u);
    if (lightRows > 0) {
        bgfx::updateTexture2D(m_lightDataTex, 0, 0, 0, 0, 4,
                              static_cast<std::uint16_t>(lightRows),
                              bgfx::copy(lights.data(), lightRows * static_cast<std::uint32_t>(sizeof(GpuLight))));
    }

    m_clusterParams[0] = 1.0f;
    m_clusterParams[1] = static_cast<float>(culler.DirectionalCount());
    m_clusterParams[2] = grid.nearPlane;
    m_clusterParams[3] = grid.farPlane;
    m_activeRangeTex = rangeTex;
    m_activeIndexTex = indexTex;
}

void BgfxSceneUniforms::ApplyLighting(const CameraView* camera, const RenderLight* lights,
                                      std::uint32_t lightCount, const SkyEnvironment* sky)
{
    // Pack the frame's lights into the parallel vec4 arrays fs_mesh unpacks.
    // Anything past kMaxRenderLights is dropped with a diagnostic; the shader
    // reads only the first `count` entries so the tail can stay uninitialized.
    std::uint32_t count = lightCount;
    if (count > kMaxRenderLights) {
        // ApplyLighting runs once per batch/pass (many times per frame), so this
        // diagnostic must be throttled — logging it unconditionally serializes
        // every draw behind a mutex + fflush(stderr) and tanks frame time.
        static std::uint32_t s_lastLoggedCount = 0;
        if (lightCount != s_lastLoggedCount) {
            s_lastLoggedCount = lightCount;
            Debug::Logger::Debug("Render", "%u lights exceeds the %u-light cap; rendering the first %u",
                                 lightCount, kMaxRenderLights, kMaxRenderLights);
        }
        count = kMaxRenderLights;
    }

    float posType[kMaxRenderLights][4] = {};
    float dirRange[kMaxRenderLights][4] = {};
    float color[kMaxRenderLights][4] = {};
    float spot[kMaxRenderLights][4] = {};
    for (std::uint32_t i = 0; i < count; ++i) {
        const RenderLight& light = lights[i];
        posType[i][0] = light.position[0];
        posType[i][1] = light.position[1];
        posType[i][2] = light.position[2];
        posType[i][3] = static_cast<float>(light.type);

        dirRange[i][0] = light.direction[0];
        dirRange[i][1] = light.direction[1];
        dirRange[i][2] = light.direction[2];
        dirRange[i][3] = light.range > 0.0f ? light.range : 1.0f;

        float rgb[4];
        ColorToFloat4(rgb, light.color);
        color[i][0] = rgb[0];
        color[i][1] = rgb[1];
        color[i][2] = rgb[2];
        color[i][3] = light.intensity;

        spot[i][0] = std::cos(light.innerAngleDegrees * 3.14159265358979323846f / 180.0f);
        spot[i][1] = std::cos(light.outerAngleDegrees * 3.14159265358979323846f / 180.0f);
        // z: source radius for point/spot sphere attenuation (fs_mesh).
        spot[i][2] = light.sourceRadius;
        spot[i][3] = 0.0f;
    }

    // Always upload the full arrays (the tail past `count` is zero-filled and
    // never read by the shader) so no light uniform is ever left unset, even
    // in a scene with no lights.
    bgfx::setUniform(m_uLightPosType, posType, kMaxRenderLights);
    bgfx::setUniform(m_uLightDirRange, dirRange, kMaxRenderLights);
    bgfx::setUniform(m_uLightColor, color, kMaxRenderLights);
    bgfx::setUniform(m_uLightSpot, spot, kMaxRenderLights);

    // Indirect fill is the scene sky colour (see SkyEnvironment) at ambient
    // intensity; the w slot carries the active light count for the shader.
    float ambient[4] = {0.0f, 0.0f, 0.0f, static_cast<float>(count)};
    if (sky != nullptr) {
        float ambientIntensity = sky->ambientIntensity;
        if (sky->mode == SkyMode::Procedural) {
            const float daylight = SkyDaylightFactor(lights, count);
            ambientIntensity = sky->nightAmbientIntensity
                + (sky->ambientIntensity - sky->nightAmbientIntensity) * daylight;
        }
        SkyAmbientColor(*sky, ambientIntensity, ambient);
    } else {
        SkyAmbientColor(ambient);
    }
    ambient[3] = static_cast<float>(count);
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

    // Forward+ clustered light data (fs_mesh stages 9-11) + mode/near/far. Bound
    // every draw; sampled only when u_clusterParams.x >= 0.5 (else classic path).
    bgfx::setTexture(9, m_sLightData, m_lightDataTex);
    bgfx::setTexture(10, m_sClusterRanges,
                     bgfx::isValid(m_activeRangeTex) ? m_activeRangeTex : m_clusterRangeTex);
    bgfx::setTexture(11, m_sLightIndices,
                     bgfx::isValid(m_activeIndexTex) ? m_activeIndexTex : m_lightIndexTex);
    bgfx::setUniform(m_uClusterParams, m_clusterParams);
}

} // namespace Concord
