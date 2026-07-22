#include "engine/render/environment/BgfxSkyRenderer.h"

#include "engine/debug/Logger.h"
#include "engine/render/shaders/generated/fs_sky.bin.h"
#include "engine/render/shaders/generated/vs_sky.bin.h"

#include <bgfx/embedded_shader.h>
#include <bx/math.h>

#include <algorithm>

namespace {

const bgfx::EmbeddedShader kSkyShaders[] = {
    {
        "vs_sky",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, vs_sky)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    {
        "fs_sky",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, fs_sky)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    BGFX_EMBEDDED_SHADER_END()
};

void UnpackColor(std::uint32_t packed, float out[4]) noexcept
{
    out[0] = static_cast<float>((packed >> 24) & 0xffu) / 255.0f;
    out[1] = static_cast<float>((packed >> 16) & 0xffu) / 255.0f;
    out[2] = static_cast<float>((packed >> 8) & 0xffu) / 255.0f;
    out[3] = static_cast<float>(packed & 0xffu) / 255.0f;
}

const Concord::RenderLight* FindSun(
    const Concord::RenderLight* lights, std::uint32_t lightCount) noexcept
{
    if (lights == nullptr) {
        return nullptr;
    }
    const std::uint32_t count = std::min(lightCount, Concord::kMaxRenderLights);
    const Concord::RenderLight* directional = nullptr;
    for (std::uint32_t index = 0; index < count; ++index) {
        if (lights[index].type == Concord::LightType::Directional) {
            if (directional == nullptr) {
                directional = &lights[index];
            }
            if (lights[index].sun) {
                return &lights[index];
            }
        }
    }
    return directional;
}

} // namespace

namespace Concord {

bool BgfxSkyRenderer::EnsureReady()
{
    if (m_ready || m_attempted) {
        return m_ready;
    }
    m_attempted = true;

    const bgfx::RendererType::Enum type = bgfx::getRendererType();
    const bgfx::ShaderHandle vertex = bgfx::createEmbeddedShader(kSkyShaders, type, "vs_sky");
    const bgfx::ShaderHandle fragment = bgfx::createEmbeddedShader(kSkyShaders, type, "fs_sky");
    if (!bgfx::isValid(vertex) || !bgfx::isValid(fragment)) {
        Debug::Logger::Error("Render", "sky shader creation failed");
        if (bgfx::isValid(vertex)) {
            bgfx::destroy(vertex);
        }
        if (bgfx::isValid(fragment)) {
            bgfx::destroy(fragment);
        }
        DestroyResources();
        return false;
    }
    m_program = bgfx::createProgram(vertex, fragment, true);

    m_uInvViewProj = bgfx::createUniform("u_skyInvViewProj", bgfx::UniformType::Mat4);
    m_uCamera = bgfx::createUniform("u_skyCamera", bgfx::UniformType::Vec4);
    m_uSolid = bgfx::createUniform("u_skySolid", bgfx::UniformType::Vec4);
    m_uZenith = bgfx::createUniform("u_skyZenith", bgfx::UniformType::Vec4);
    m_uHorizon = bgfx::createUniform("u_skyHorizon", bgfx::UniformType::Vec4);
    m_uGround = bgfx::createUniform("u_skyGround", bgfx::UniformType::Vec4);
    m_uParams = bgfx::createUniform("u_skyParams", bgfx::UniformType::Vec4);
    m_uOptions = bgfx::createUniform("u_skyOptions", bgfx::UniformType::Vec4);
    m_uSunDirection = bgfx::createUniform("u_skySunDirection", bgfx::UniformType::Vec4);
    m_uSunColor = bgfx::createUniform("u_skySunColor", bgfx::UniformType::Vec4);
    m_uCloudLayer = bgfx::createUniform("u_cloudLayer", bgfx::UniformType::Vec4);
    m_uCloudShape = bgfx::createUniform("u_cloudShape", bgfx::UniformType::Vec4);
    m_uCloudMotion = bgfx::createUniform("u_cloudMotion", bgfx::UniformType::Vec4);
    m_uCloudLit = bgfx::createUniform("u_cloudLit", bgfx::UniformType::Vec4);
    m_uCloudShadow = bgfx::createUniform("u_cloudShadow", bgfx::UniformType::Vec4);
    m_uCloudFire = bgfx::createUniform("u_cloudFire", bgfx::UniformType::Vec4);
    m_uFogParams = bgfx::createUniform("u_fogParams", bgfx::UniformType::Vec4);
    m_uFogColor = bgfx::createUniform("u_fogColor", bgfx::UniformType::Vec4);

    m_layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
    static const float kTriangle[] = {
        -1.0f, -1.0f, 0.0f,
         3.0f, -1.0f, 0.0f,
        -1.0f,  3.0f, 0.0f,
    };
    m_fullscreenVb = bgfx::createVertexBuffer(bgfx::copy(kTriangle, sizeof(kTriangle)), m_layout);

    m_ready = bgfx::isValid(m_program)
        && bgfx::isValid(m_uInvViewProj)
        && bgfx::isValid(m_uCamera)
        && bgfx::isValid(m_uSolid)
        && bgfx::isValid(m_uZenith)
        && bgfx::isValid(m_uHorizon)
        && bgfx::isValid(m_uGround)
        && bgfx::isValid(m_uParams)
        && bgfx::isValid(m_uOptions)
        && bgfx::isValid(m_uSunDirection)
        && bgfx::isValid(m_uSunColor)
        && bgfx::isValid(m_uCloudLayer)
        && bgfx::isValid(m_uCloudShape)
        && bgfx::isValid(m_uCloudMotion)
        && bgfx::isValid(m_uCloudLit)
        && bgfx::isValid(m_uCloudShadow)
        && bgfx::isValid(m_uCloudFire)
        && bgfx::isValid(m_uFogParams)
        && bgfx::isValid(m_uFogColor)
        && bgfx::isValid(m_fullscreenVb);
    if (!m_ready) {
        Debug::Logger::Error("Render", "sky renderer resource initialization failed");
        DestroyResources();
    }
    return m_ready;
}

void BgfxSkyRenderer::DestroyResources()
{
    if (bgfx::isValid(m_fullscreenVb)) {
        bgfx::destroy(m_fullscreenVb);
        m_fullscreenVb = BGFX_INVALID_HANDLE;
    }
    for (bgfx::UniformHandle* uniform : {
             &m_uInvViewProj, &m_uCamera, &m_uSolid, &m_uZenith, &m_uHorizon,
             &m_uGround, &m_uParams, &m_uOptions, &m_uSunDirection, &m_uSunColor}) {
        if (bgfx::isValid(*uniform)) {
            bgfx::destroy(*uniform);
            *uniform = BGFX_INVALID_HANDLE;
        }
    }
    for (bgfx::UniformHandle* uniform : {
             &m_uCloudLayer, &m_uCloudShape, &m_uCloudMotion, &m_uCloudLit,
             &m_uCloudShadow, &m_uCloudFire, &m_uFogParams, &m_uFogColor}) {
        if (bgfx::isValid(*uniform)) {
            bgfx::destroy(*uniform);
            *uniform = BGFX_INVALID_HANDLE;
        }
    }
    if (bgfx::isValid(m_program)) {
        bgfx::destroy(m_program);
        m_program = BGFX_INVALID_HANDLE;
    }
    m_ready = false;
}

void BgfxSkyRenderer::Shutdown()
{
    DestroyResources();
    m_attempted = false;
}

void BgfxSkyRenderer::Draw(RenderViewHandle view, const float viewMatrix[16],
                           const float projectionMatrix[16], const float eye[3],
                           const SkyEnvironment& environment,
                           const RenderLight* lights, std::uint32_t lightCount,
                           bool linearOutput, bool drawClouds)
{
    if (view == kInvalidRenderView || viewMatrix == nullptr
        || projectionMatrix == nullptr || eye == nullptr || !EnsureReady()) {
        return;
    }

    float viewProjection[16];
    float inverseViewProjection[16];
    bx::mtxMul(viewProjection, viewMatrix, projectionMatrix);
    bx::mtxInverse(inverseViewProjection, viewProjection);

    float solid[4];
    float zenith[4];
    float horizon[4];
    float ground[4];
    float cloudLit[4];
    float cloudShadow[4];
    float cloudFire[4];
    float fogColor[4];
    UnpackColor(environment.solidColor, solid);
    UnpackColor(environment.zenithColor, zenith);
    UnpackColor(environment.horizonColor, horizon);
    UnpackColor(environment.groundColor, ground);
    UnpackColor(environment.cloudLitColor, cloudLit);
    UnpackColor(environment.cloudShadowColor, cloudShadow);
    UnpackColor(environment.cloudFireColor, cloudFire);
    UnpackColor(environment.fogColor, fogColor);

    const RenderLight* sun = FindSun(lights, lightCount);
    float sunDirection[4] = {0.0f, -1.0f, 0.0f, 0.2666f};
    float sunColor[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    if (sun != nullptr) {
        std::copy_n(sun->direction, 3, sunDirection);
        sunDirection[3] = std::max(sun->directionalAngularRadiusDegrees, 0.01f);
        UnpackColor(sun->color, sunColor);
        sunColor[3] = std::max(sun->intensity, 0.0f);
    }

    const float camera[4] = {
        eye[0], eye[1], eye[2], bgfx::getCaps()->homogeneousDepth ? -1.0f : 0.0f,
    };
    const float params[4] = {
        static_cast<float>(environment.mode == SkyMode::Procedural ? 1 : 0),
        std::max(environment.intensity, 0.0f),
        std::max(environment.horizonFalloff, 0.05f),
        linearOutput ? 1.0f : 0.0f,
    };
    const float options[4] = {
        environment.sunDisk && sun != nullptr && (!sun->sun || sun->visibleDisk) ? 1.0f : 0.0f,
        std::max(environment.sunDiskIntensity, 0.0f)
            * (sun != nullptr && sun->sun ? sun->visibleDiskIntensity : 1.0f),
        sun != nullptr ? 1.0f : 0.0f,
        0.0f,
    };
    const float cloudLayer[4] = {
        environment.clouds && drawClouds ? 1.0f : 0.0f,
        environment.cloudBaseHeight,
        environment.cloudThickness,
        environment.cloudDensity,
    };
    const float cloudShape[4] = {
        environment.cloudCoverage,
        environment.cloudScale,
        environment.cloudErosion,
        environment.cloudDetail,
    };
    const float cloudMotion[4] = {
        environment.cloudOffsetEast,
        environment.cloudOffsetNorth,
        environment.cloudSilverLining,
        environment.cloudFireEmission,
    };
    const float fogParams[4] = {
        environment.volumetricFog ? 1.0f : 0.0f,
        environment.fogDensity,
        environment.fogBaseHeight,
        environment.fogHeightFalloff,
    };

    bgfx::setUniform(m_uInvViewProj, inverseViewProjection);
    bgfx::setUniform(m_uCamera, camera);
    bgfx::setUniform(m_uSolid, solid);
    bgfx::setUniform(m_uZenith, zenith);
    bgfx::setUniform(m_uHorizon, horizon);
    bgfx::setUniform(m_uGround, ground);
    bgfx::setUniform(m_uParams, params);
    bgfx::setUniform(m_uOptions, options);
    bgfx::setUniform(m_uSunDirection, sunDirection);
    bgfx::setUniform(m_uSunColor, sunColor);
    bgfx::setUniform(m_uCloudLayer, cloudLayer);
    bgfx::setUniform(m_uCloudShape, cloudShape);
    bgfx::setUniform(m_uCloudMotion, cloudMotion);
    bgfx::setUniform(m_uCloudLit, cloudLit);
    bgfx::setUniform(m_uCloudShadow, cloudShadow);
    bgfx::setUniform(m_uCloudFire, cloudFire);
    bgfx::setUniform(m_uFogParams, fogParams);
    bgfx::setUniform(m_uFogColor, fogColor);

    bgfx::setViewMode(view, bgfx::ViewMode::Sequential);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::setVertexBuffer(0, m_fullscreenVb);
    bgfx::submit(view, m_program);
}

} // namespace Concord
