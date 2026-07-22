#include "engine/render/raytrace/RayTracer.h"

#include "engine/debug/Logger.h"
#include "engine/render/shaders/generated/cs_raytrace.bin.h"
#include "engine/render/shaders/generated/fs_rtresolve.bin.h"
#include "engine/render/shaders/generated/vs_fullscreen.bin.h"

#include <bgfx/embedded_shader.h>

#include <algorithm>
#include <cstring>

namespace {

const bgfx::EmbeddedShader kRayTraceShaders[] = {
    {
        "cs_raytrace",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, cs_raytrace)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    {
        "vs_fullscreen",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, vs_fullscreen)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    {
        "fs_rtresolve",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, fs_rtresolve)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    BGFX_EMBEDDED_SHADER_END()
};

} // namespace

namespace Concord {

RayTracer::~RayTracer()
{
    Shutdown();
}

bool RayTracer::Supported() const
{
    return (bgfx::getCaps()->supported & BGFX_CAPS_COMPUTE) != 0;
}

bool RayTracer::EnsureReady()
{
    if (m_ready || m_attempted) {
        return m_ready;
    }
    m_attempted = true;

    if (!Supported()) {
        // Not an error: the backend keeps rasterizing every object, so a
        // ray-traced flag simply has no visible effect on this renderer.
        Debug::Logger::Info("Render", "compute unsupported; ray tracing path disabled");
        return false;
    }

    const bgfx::RendererType::Enum type = bgfx::getRendererType();
    const bgfx::ShaderHandle cs = bgfx::createEmbeddedShader(kRayTraceShaders, type, "cs_raytrace");
    if (!bgfx::isValid(cs)) {
        Debug::Logger::Error("Render", "ray tracing compute shader creation failed");
        return false;
    }
    m_computeProgram = bgfx::createProgram(cs, true);
    if (!bgfx::isValid(m_computeProgram)) {
        Debug::Logger::Error("Render", "ray tracing compute program init failed");
        return false;
    }

    const bgfx::ShaderHandle vs = bgfx::createEmbeddedShader(kRayTraceShaders, type, "vs_fullscreen");
    const bgfx::ShaderHandle fs = bgfx::createEmbeddedShader(kRayTraceShaders, type, "fs_rtresolve");
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
        Debug::Logger::Error("Render", "ray tracing resolve shader creation failed");
        if (bgfx::isValid(vs)) { bgfx::destroy(vs); }
        if (bgfx::isValid(fs)) { bgfx::destroy(fs); }
        DestroyResources();
        return false;
    }
    m_resolveProgram = bgfx::createProgram(vs, fs, true);
    if (!bgfx::isValid(m_resolveProgram)) {
        Debug::Logger::Error("Render", "ray tracing resolve program init failed");
        DestroyResources();
        return false;
    }

    m_uParams        = bgfx::createUniform("u_rtParams",      bgfx::UniformType::Vec4);
    m_uCamera        = bgfx::createUniform("u_rtCamera",      bgfx::UniformType::Vec4);
    m_uOptions       = bgfx::createUniform("u_rtOptions",     bgfx::UniformType::Vec4);
    m_uLight         = bgfx::createUniform("u_rtLight",       bgfx::UniformType::Vec4);
    m_uSun           = bgfx::createUniform("u_rtSun",         bgfx::UniformType::Vec4);
    m_uSky           = bgfx::createUniform("u_rtSky",         bgfx::UniformType::Vec4);
    m_uInvViewProj   = bgfx::createUniform("u_rtInvViewProj", bgfx::UniformType::Mat4);
    m_uViewProj      = bgfx::createUniform("u_rtViewProj",    bgfx::UniformType::Mat4);
    m_uSpheres       = bgfx::createUniform("u_rtSpheres",     bgfx::UniformType::Vec4, kMaxSpheres * 3);
    m_uResolveParams = bgfx::createUniform("u_presentParams", bgfx::UniformType::Vec4);
    m_sRtColor       = bgfx::createUniform("s_rtColor",       bgfx::UniformType::Sampler);
    m_sRtEnvironment = bgfx::createUniform("s_rtEnvironment", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(m_uParams) || !bgfx::isValid(m_uCamera) || !bgfx::isValid(m_uOptions)
        || !bgfx::isValid(m_uLight) || !bgfx::isValid(m_uSun) || !bgfx::isValid(m_uSky)
        || !bgfx::isValid(m_uInvViewProj) || !bgfx::isValid(m_uViewProj)
        || !bgfx::isValid(m_uSpheres) || !bgfx::isValid(m_uResolveParams)
        || !bgfx::isValid(m_sRtColor) || !bgfx::isValid(m_sRtEnvironment)) {
        Debug::Logger::Error("Render", "ray tracing uniform creation failed");
        DestroyResources();
        return false;
    }

    m_layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
    static const float kTriangle[] = {
        -1.0f, -1.0f, 0.0f,
         3.0f, -1.0f, 0.0f,
        -1.0f,  3.0f, 0.0f,
    };
    m_fullscreenVb = bgfx::createVertexBuffer(bgfx::copy(kTriangle, sizeof(kTriangle)), m_layout);
    if (!bgfx::isValid(m_fullscreenVb)) {
        Debug::Logger::Error("Render", "ray tracing fullscreen buffer creation failed");
        DestroyResources();
        return false;
    }

    m_ready = true;
    return true;
}

void RayTracer::DestroyResources()
{
    if (bgfx::isValid(m_fullscreenVb)) {
        bgfx::destroy(m_fullscreenVb);
        m_fullscreenVb = BGFX_INVALID_HANDLE;
    }
    for (bgfx::UniformHandle* u : {&m_uParams, &m_uCamera, &m_uOptions, &m_uLight,
                                   &m_uSun, &m_uSky, &m_uInvViewProj, &m_uViewProj,
                                   &m_uSpheres, &m_uResolveParams, &m_sRtColor,
                                   &m_sRtEnvironment}) {
        if (bgfx::isValid(*u)) {
            bgfx::destroy(*u);
            *u = BGFX_INVALID_HANDLE;
        }
    }
    if (bgfx::isValid(m_resolveProgram)) {
        bgfx::destroy(m_resolveProgram);
        m_resolveProgram = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_computeProgram)) {
        bgfx::destroy(m_computeProgram);
        m_computeProgram = BGFX_INVALID_HANDLE;
    }
    m_ready = false;
}

void RayTracer::Shutdown()
{
    DestroyResources();
    m_attempted = false;
}

bool RayTracer::CreateImage(std::uint32_t width, std::uint32_t height, bgfx::TextureHandle& tex) const
{
    tex = BGFX_INVALID_HANDLE;
    if (width == 0 || height == 0) {
        return false;
    }
    // Compute-writable and sampleable in the resolve pass. Point-sampled and
    // clamped so packed coverage/depth is never blended across pixels.
    const std::uint64_t flags = BGFX_TEXTURE_COMPUTE_WRITE
        | BGFX_SAMPLER_POINT
        | BGFX_SAMPLER_U_CLAMP
        | BGFX_SAMPLER_V_CLAMP;
    tex = bgfx::createTexture2D(static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height),
                                false, 1, bgfx::TextureFormat::RGBA32F, flags);
    if (!bgfx::isValid(tex)) {
        Debug::Logger::Error("Render", "ray tracing image creation failed at %ux%u", width, height);
        return false;
    }
    return true;
}

void RayTracer::DestroyImage(bgfx::TextureHandle& tex) const
{
    if (bgfx::isValid(tex)) {
        bgfx::destroy(tex);
        tex = BGFX_INVALID_HANDLE;
    }
}

void RayTracer::Trace(RenderViewHandle computeView, RenderViewHandle resolveView,
                      bgfx::TextureHandle target, std::uint32_t width, std::uint32_t height,
                      const float invViewProj[16], const float viewProj[16],
                      const float camPos[3], const float lightDir[3], const float sunColor[3],
                      const float skyColor[3], float skyAmbient,
                      bgfx::TextureHandle environment,
                      const Sphere* spheres, std::uint32_t count)
{
    if (!m_ready || !bgfx::isValid(target) || !bgfx::isValid(environment) || count == 0) {
        return;
    }
    count = std::min(count, kMaxSpheres);

    const float params[4] = {
        static_cast<float>(width),
        static_cast<float>(height),
        static_cast<float>(count),
        0.0f,
    };
    const float camera[4] = {camPos[0], camPos[1], camPos[2], 0.0f};
    const float options[4] = {
        bgfx::getCaps()->homogeneousDepth ? 1.0f : 0.0f,
        bgfx::getCaps()->originBottomLeft ? 1.0f : 0.0f,
        1.0f, 0.0f,
    };
    const float light[4] = {lightDir[0], lightDir[1], lightDir[2], 0.0f};
    // Real linear sun radiance and sky colour so the traced spheres reflect the
    // same environment and key light as the rasterized scene, not a fixed sky.
    const float sun[4] = {sunColor[0], sunColor[1], sunColor[2], 0.0f};
    const float sky[4] = {skyColor[0], skyColor[1], skyColor[2], skyAmbient};
    float sphereData[kMaxSpheres * 3 * 4] = {};
    for (std::uint32_t i = 0; i < count; ++i) {
        float* v0 = &sphereData[(i * 3 + 0) * 4];
        float* v1 = &sphereData[(i * 3 + 1) * 4];
        float* v2 = &sphereData[(i * 3 + 2) * 4];
        v0[0] = spheres[i].center[0];
        v0[1] = spheres[i].center[1];
        v0[2] = spheres[i].center[2];
        v0[3] = spheres[i].radius;
        v1[0] = spheres[i].color[0];
        v1[1] = spheres[i].color[1];
        v1[2] = spheres[i].color[2];
        v1[3] = spheres[i].reflectivity;
        v2[0] = spheres[i].roughness;
        v2[1] = spheres[i].metallic;
    }

    bgfx::setUniform(m_uParams, params);
    bgfx::setUniform(m_uCamera, camera);
    bgfx::setUniform(m_uOptions, options);
    bgfx::setUniform(m_uLight, light);
    bgfx::setUniform(m_uSun, sun);
    bgfx::setUniform(m_uSky, sky);
    bgfx::setUniform(m_uInvViewProj, invViewProj);
    bgfx::setUniform(m_uViewProj, viewProj);
    bgfx::setUniform(m_uSpheres, sphereData, kMaxSpheres * 3);

    bgfx::setImage(0, target, 0, bgfx::Access::Write, bgfx::TextureFormat::RGBA32F);
    constexpr std::uint32_t kEnvironmentSampler = BGFX_SAMPLER_U_CLAMP
        | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_W_CLAMP;
    bgfx::setTexture(1, m_sRtEnvironment, environment, kEnvironmentSampler);
    const std::uint32_t groupsX = (width + 15) / 16;
    const std::uint32_t groupsY = (height + 15) / 16;
    bgfx::dispatch(computeView, m_computeProgram, groupsX, groupsY, 1);

    // Compute images use texture-row orientation, while a fullscreen draw into
    // the Vulkan offscreen scene target uses the render-target orientation.
    // Reuse vs_fullscreen's V flip so traced colour and packed depth land on
    // the same pixels as the rasterized scene instead of appearing mirrored.
    const float resolveParams[4] = {
        bgfx::getCaps()->originBottomLeft ? 0.0f : 1.0f,
        0.0f, 0.0f, 0.0f,
    };
    bgfx::setUniform(m_uResolveParams, resolveParams);
    bgfx::setTexture(0, m_sRtColor, target);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_Z
                   | BGFX_STATE_DEPTH_TEST_LEQUAL | BGFX_STATE_BLEND_ALPHA);
    bgfx::setVertexBuffer(0, m_fullscreenVb);
    bgfx::submit(resolveView, m_resolveProgram);
}

} // namespace Concord
