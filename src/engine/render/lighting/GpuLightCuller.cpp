#include "engine/render/lighting/GpuLightCuller.h"

#include "engine/debug/Logger.h"
#include "engine/render/shaders/generated/cs_light_cull.bin.h"

#include <bgfx/embedded_shader.h>
#include <bx/math.h>

namespace {

const bgfx::EmbeddedShader kLightCullShaders[] = {
    {
        "cs_light_cull",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, cs_light_cull)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    BGFX_EMBEDDED_SHADER_END()
};

} // namespace

namespace Concord {

GpuLightCuller::~GpuLightCuller()
{
    DestroyResources();
}

bool GpuLightCuller::Supported() const
{
    return (bgfx::getCaps()->supported & BGFX_CAPS_COMPUTE) != 0;
}

bool GpuLightCuller::EnsureReady()
{
    if (m_ready || m_attempted) {
        return m_ready;
    }
    m_attempted = true;

    if (!Supported()) {
        Debug::Logger::Debug("Render", "compute unsupported; Forward+ stays on the CPU light culler");
        return false;
    }

    const bgfx::RendererType::Enum type = bgfx::getRendererType();
    const bgfx::ShaderHandle cs = bgfx::createEmbeddedShader(kLightCullShaders, type, "cs_light_cull");
    if (!bgfx::isValid(cs)) {
        Debug::Logger::Error("Render", "light-cull compute shader creation failed");
        return false;
    }
    m_program = bgfx::createProgram(cs, true);

    m_sLightData = bgfx::createUniform("s_lightData", bgfx::UniformType::Sampler);
    m_uCullParams = bgfx::createUniform("u_cullParams", bgfx::UniformType::Vec4);
    m_uCullScreen = bgfx::createUniform("u_cullScreen", bgfx::UniformType::Vec4);
    m_uCullView = bgfx::createUniform("u_cullView", bgfx::UniformType::Mat4);
    m_uCullViewProj = bgfx::createUniform("u_cullViewProj", bgfx::UniformType::Mat4);

    constexpr std::uint64_t kFlags = BGFX_TEXTURE_COMPUTE_WRITE
        | BGFX_SAMPLER_POINT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    m_rangeTex = bgfx::createTexture2D(
        static_cast<std::uint16_t>(ClusterGrid::kDimX * ClusterGrid::kDimY),
        static_cast<std::uint16_t>(ClusterGrid::kDimZ), false, 1,
        bgfx::TextureFormat::RGBA32F, kFlags);
    m_indexTex = bgfx::createTexture2D(1024, 256, false, 1, bgfx::TextureFormat::R32F, kFlags);

    m_ready = bgfx::isValid(m_program) && bgfx::isValid(m_sLightData)
        && bgfx::isValid(m_uCullParams) && bgfx::isValid(m_uCullScreen)
        && bgfx::isValid(m_uCullView) && bgfx::isValid(m_uCullViewProj)
        && bgfx::isValid(m_rangeTex) && bgfx::isValid(m_indexTex);
    if (!m_ready) {
        Debug::Logger::Error("Render", "GPU light-cull resource initialization failed");
        DestroyResources();
    }
    return m_ready;
}

void GpuLightCuller::DestroyResources()
{
    for (bgfx::UniformHandle* u : {&m_sLightData, &m_uCullParams, &m_uCullScreen,
                                   &m_uCullView, &m_uCullViewProj}) {
        if (bgfx::isValid(*u)) {
            bgfx::destroy(*u);
            *u = BGFX_INVALID_HANDLE;
        }
    }
    for (bgfx::TextureHandle* t : {&m_rangeTex, &m_indexTex}) {
        if (bgfx::isValid(*t)) {
            bgfx::destroy(*t);
            *t = BGFX_INVALID_HANDLE;
        }
    }
    if (bgfx::isValid(m_program)) {
        bgfx::destroy(m_program);
        m_program = BGFX_INVALID_HANDLE;
    }
    m_ready = false;
}

void GpuLightCuller::Shutdown()
{
    DestroyResources();
    m_attempted = false;
}

void GpuLightCuller::Cull(RenderViewHandle view, bgfx::TextureHandle lightDataTex,
                          std::uint32_t lightCount, std::uint32_t directionalCount,
                          const float viewMatrix[16], const float viewProj[16],
                          const ClusterGrid& grid)
{
    if (!Ready() || !bgfx::isValid(lightDataTex)) {
        return;
    }

    const float params[4] = {
        static_cast<float>(lightCount), static_cast<float>(directionalCount),
        grid.nearPlane, grid.farPlane,
    };
    const float screen[4] = {
        static_cast<float>(grid.screenWidth), static_cast<float>(grid.screenHeight),
        grid.tanHalfFovY, grid.aspect,
    };
    bgfx::setUniform(m_uCullParams, params);
    bgfx::setUniform(m_uCullScreen, screen);
    bgfx::setUniform(m_uCullView, viewMatrix);
    bgfx::setUniform(m_uCullViewProj, viewProj);
    bgfx::setTexture(0, m_sLightData, lightDataTex);
    bgfx::setImage(1, m_rangeTex, 0, bgfx::Access::Write);
    bgfx::setImage(2, m_indexTex, 0, bgfx::Access::Write);
    // NUM_THREADS(kDimX, kDimY, 1): one dispatch group per depth slice covers
    // the whole X/Y tile grid in its threads.
    bgfx::dispatch(view, m_program, 1, 1, ClusterGrid::kDimZ);
}

} // namespace Concord
