#include "engine/render/volume/BgfxVolumeCloudRenderer.h"

#include "engine/debug/Logger.h"
#include "engine/render/shaders/generated/fs_volcloud.bin.h"
#include "engine/render/shaders/generated/fs_volcloud_composite.bin.h"
#include "engine/render/shaders/generated/vs_volcloud.bin.h"
#include "engine/render/shaders/generated/vs_volcloud_composite.bin.h"

#include <bgfx/embedded_shader.h>
#include <bx/math.h>

#include <algorithm>

namespace {

const bgfx::EmbeddedShader kVolumeCloudShaders[] = {
    {
        "vs_volcloud",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, vs_volcloud)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    {
        "fs_volcloud",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, fs_volcloud)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    {
        "vs_volcloud_composite",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, vs_volcloud_composite)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    {
        "fs_volcloud_composite",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, fs_volcloud_composite)
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

/** First directional light in the frame drives the cloud sun; matches BgfxSkyRenderer. */
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

bool BgfxVolumeCloudRenderer::EnsureReady()
{
    if (m_ready || m_attempted) {
        return m_ready;
    }
    m_attempted = true;

    const bgfx::RendererType::Enum type = bgfx::getRendererType();
    const bgfx::ShaderHandle marchVs = bgfx::createEmbeddedShader(kVolumeCloudShaders, type, "vs_volcloud");
    const bgfx::ShaderHandle marchFs = bgfx::createEmbeddedShader(kVolumeCloudShaders, type, "fs_volcloud");
    const bgfx::ShaderHandle compVs = bgfx::createEmbeddedShader(kVolumeCloudShaders, type, "vs_volcloud_composite");
    const bgfx::ShaderHandle compFs = bgfx::createEmbeddedShader(kVolumeCloudShaders, type, "fs_volcloud_composite");
    const auto destroyIfValid = [](bgfx::ShaderHandle handle) {
        if (bgfx::isValid(handle)) {
            bgfx::destroy(handle);
        }
    };
    if (!bgfx::isValid(marchVs) || !bgfx::isValid(marchFs)
        || !bgfx::isValid(compVs) || !bgfx::isValid(compFs)) {
        Debug::Logger::Error("Render", "volumetric cloud shader creation failed");
        destroyIfValid(marchVs);
        destroyIfValid(marchFs);
        destroyIfValid(compVs);
        destroyIfValid(compFs);
        DestroyResources();
        return false;
    }
    m_marchProgram = bgfx::createProgram(marchVs, marchFs, true);
    m_compositeProgram = bgfx::createProgram(compVs, compFs, true);

    m_uInvViewProj = bgfx::createUniform("u_vcInvViewProj", bgfx::UniformType::Mat4);
    m_uCamera = bgfx::createUniform("u_vcCamera", bgfx::UniformType::Vec4);
    m_uLayer = bgfx::createUniform("u_vcLayer", bgfx::UniformType::Vec4);
    m_uShape = bgfx::createUniform("u_vcShape", bgfx::UniformType::Vec4);
    m_uMotion = bgfx::createUniform("u_vcMotion", bgfx::UniformType::Vec4);
    m_uLit = bgfx::createUniform("u_vcLit", bgfx::UniformType::Vec4);
    m_uShadow = bgfx::createUniform("u_vcShadow", bgfx::UniformType::Vec4);
    m_uFire = bgfx::createUniform("u_vcFire", bgfx::UniformType::Vec4);
    m_uSunDir = bgfx::createUniform("u_vcSunDir", bgfx::UniformType::Vec4);
    m_sSceneDepth = bgfx::createUniform("s_vcSceneDepth", bgfx::UniformType::Sampler);
    m_sCloud = bgfx::createUniform("s_vcCloud", bgfx::UniformType::Sampler);

    m_layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
    static const float kTriangle[] = {
        -1.0f, -1.0f, 0.0f,
         3.0f, -1.0f, 0.0f,
        -1.0f,  3.0f, 0.0f,
    };
    m_fullscreenVb = bgfx::createVertexBuffer(bgfx::copy(kTriangle, sizeof(kTriangle)), m_layout);

    m_ready = bgfx::isValid(m_marchProgram)
        && bgfx::isValid(m_compositeProgram)
        && bgfx::isValid(m_uInvViewProj)
        && bgfx::isValid(m_uCamera)
        && bgfx::isValid(m_uLayer)
        && bgfx::isValid(m_uShape)
        && bgfx::isValid(m_uMotion)
        && bgfx::isValid(m_uLit)
        && bgfx::isValid(m_uShadow)
        && bgfx::isValid(m_uFire)
        && bgfx::isValid(m_uSunDir)
        && bgfx::isValid(m_sSceneDepth)
        && bgfx::isValid(m_sCloud)
        && bgfx::isValid(m_fullscreenVb);
    if (!m_ready) {
        Debug::Logger::Error("Render", "volumetric cloud resource initialization failed");
        DestroyResources();
    }
    return m_ready;
}

void BgfxVolumeCloudRenderer::DestroyResources()
{
    if (bgfx::isValid(m_fullscreenVb)) {
        bgfx::destroy(m_fullscreenVb);
        m_fullscreenVb = BGFX_INVALID_HANDLE;
    }
    for (bgfx::UniformHandle* uniform : {
             &m_uInvViewProj, &m_uCamera, &m_uLayer, &m_uShape, &m_uMotion,
             &m_uLit, &m_uShadow, &m_uFire, &m_uSunDir, &m_sSceneDepth, &m_sCloud}) {
        if (bgfx::isValid(*uniform)) {
            bgfx::destroy(*uniform);
            *uniform = BGFX_INVALID_HANDLE;
        }
    }
    for (bgfx::ProgramHandle* program : {&m_marchProgram, &m_compositeProgram}) {
        if (bgfx::isValid(*program)) {
            bgfx::destroy(*program);
            *program = BGFX_INVALID_HANDLE;
        }
    }
    m_ready = false;
}

void BgfxVolumeCloudRenderer::Shutdown()
{
    DestroyResources();
    m_attempted = false;
}

void BgfxVolumeCloudRenderer::Draw(const DrawParams& params, const SkyEnvironment& environment)
{
    if (params.marchView == kInvalidRenderView || params.compositeView == kInvalidRenderView
        || !bgfx::isValid(params.lowResFb) || !bgfx::isValid(params.lowResColor)
        || !bgfx::isValid(params.sceneFb) || !bgfx::isValid(params.sceneDepth)
        || params.viewMatrix == nullptr || params.projectionMatrix == nullptr
        || params.eye == nullptr || !Enabled(environment) || !EnsureReady()) {
        return;
    }

    float viewProjection[16];
    float inverseViewProjection[16];
    bx::mtxMul(viewProjection, params.viewMatrix, params.projectionMatrix);
    bx::mtxInverse(inverseViewProjection, viewProjection);

    float lit[4];
    float shadow[4];
    float fire[4];
    UnpackColor(environment.cloudLitColor, lit);
    UnpackColor(environment.cloudShadowColor, shadow);
    UnpackColor(environment.cloudFireColor, fire);

    float sunDirection[4] = {0.0f, -1.0f, 0.0f, 0.0f};
    const RenderLight* sun = FindSun(params.lights, params.lightCount);
    if (sun != nullptr) {
        std::copy_n(sun->direction, 3, sunDirection);
    }

    const float camera[4] = {
        params.eye[0], params.eye[1], params.eye[2],
        bgfx::getCaps()->homogeneousDepth ? -1.0f : 0.0f,
    };
    const float layer[4] = {
        environment.clouds ? 1.0f : 0.0f,
        environment.cloudBaseHeight,
        environment.cloudThickness,
        environment.cloudDensity,
    };
    const float shape[4] = {
        environment.cloudCoverage,
        environment.cloudScale,
        environment.cloudErosion,
        environment.cloudDetail,
    };
    const float motion[4] = {
        environment.cloudOffsetEast,
        environment.cloudOffsetNorth,
        environment.cloudSilverLining,
        environment.cloudFireEmission,
    };

    // Pass A: march the clouds at half resolution into the low-res target.
    // The target is cleared to transparent black and written opaquely (no
    // blend), so it holds the premultiplied cloud contribution.
    bgfx::setViewFrameBuffer(params.marchView, params.lowResFb);
    bgfx::setViewRect(params.marchView, 0, 0, static_cast<std::uint16_t>(params.lowWidth),
                      static_cast<std::uint16_t>(params.lowHeight));
    bgfx::setViewClear(params.marchView, BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);
    bgfx::setViewMode(params.marchView, bgfx::ViewMode::Sequential);
    bgfx::touch(params.marchView);

    bgfx::setUniform(m_uInvViewProj, inverseViewProjection);
    bgfx::setUniform(m_uCamera, camera);
    bgfx::setUniform(m_uLayer, layer);
    bgfx::setUniform(m_uShape, shape);
    bgfx::setUniform(m_uMotion, motion);
    bgfx::setUniform(m_uLit, lit);
    bgfx::setUniform(m_uShadow, shadow);
    bgfx::setUniform(m_uFire, fire);
    bgfx::setUniform(m_uSunDir, sunDirection);
    bgfx::setTexture(0, m_sSceneDepth, params.sceneDepth);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::setVertexBuffer(0, m_fullscreenVb);
    bgfx::submit(params.marchView, m_marchProgram);

    // Pass B: upsample the low-res cloud (hardware bilinear on the sampled
    // target) and composite over the full-res scene color. No clear; the scene
    // color is preserved and the cloud is layered on with premultiplied alpha.
    bgfx::setViewFrameBuffer(params.compositeView, params.sceneFb);
    bgfx::setViewRect(params.compositeView, 0, 0, static_cast<std::uint16_t>(params.fullWidth),
                      static_cast<std::uint16_t>(params.fullHeight));
    bgfx::setViewClear(params.compositeView, BGFX_CLEAR_NONE);
    bgfx::setViewMode(params.compositeView, bgfx::ViewMode::Sequential);
    bgfx::setTexture(0, m_sCloud, params.lowResColor,
                     BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
        | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA));
    bgfx::setVertexBuffer(0, m_fullscreenVb);
    bgfx::submit(params.compositeView, m_compositeProgram);
}

} // namespace Concord
