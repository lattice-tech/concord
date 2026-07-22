#include "engine/render/shadow/ShadowMap.h"

#include "engine/debug/Logger.h"
#include "engine/render/shaders/generated/fs_shadow.bin.h"
#include "engine/render/shaders/generated/vs_shadow.bin.h"
#include "engine/render/shaders/generated/vs_shadow_skinned.bin.h"

#include <bgfx/embedded_shader.h>

#include <cstring>

namespace {

const bgfx::EmbeddedShader kShadowShaders[] = {
    {
        "vs_shadow",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, vs_shadow)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    {
        "fs_shadow",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, fs_shadow)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    {
        "vs_shadow_skinned",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, vs_shadow_skinned)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    BGFX_EMBEDDED_SHADER_END()
};

} // namespace

namespace Concord {

ShadowMap::~ShadowMap()
{
    Shutdown();
}

bool ShadowMap::EnsureReady()
{
    if (m_ready || m_attempted) {
        return m_ready;
    }
    m_attempted = true;

    const bgfx::RendererType::Enum rendererType = bgfx::getRendererType();
    const bgfx::ShaderHandle vs = bgfx::createEmbeddedShader(kShadowShaders, rendererType, "vs_shadow");
    const bgfx::ShaderHandle fs = bgfx::createEmbeddedShader(kShadowShaders, rendererType, "fs_shadow");
    const bgfx::ShaderHandle skinnedVs = bgfx::createEmbeddedShader(kShadowShaders, rendererType, "vs_shadow_skinned");
    const bgfx::ShaderHandle skinnedFs = bgfx::createEmbeddedShader(kShadowShaders, rendererType, "fs_shadow");
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)
        || !bgfx::isValid(skinnedVs) || !bgfx::isValid(skinnedFs)) {
        Debug::Logger::Error("Render",
                             "shadow shader creation failed for %s",
                             bgfx::getRendererName(rendererType));
        for (bgfx::ShaderHandle shader : {vs, fs, skinnedVs, skinnedFs}) {
            if (bgfx::isValid(shader)) {
                bgfx::destroy(shader);
            }
        }
        return false;
    }

    m_program = bgfx::createProgram(vs, fs, true);
    m_skinnedProgram = bgfx::createProgram(skinnedVs, skinnedFs, true);
    if (!bgfx::isValid(m_program) || !bgfx::isValid(m_skinnedProgram)) {
        Debug::Logger::Error("Render", "shadow program init failed");
        Shutdown();
        return false;
    }

    m_uLightViewProj    = bgfx::createUniform("u_lightViewProj", bgfx::UniformType::Mat4,
                                               kShadowCascadeCount);
    m_uShadowCameraView = bgfx::createUniform("u_shadowCameraView", bgfx::UniformType::Mat4);
    m_uShadowCascadeSplits = bgfx::createUniform("u_shadowCascadeSplits", bgfx::UniformType::Vec4);
    m_uShadowCascadeBlend = bgfx::createUniform("u_shadowCascadeBlend", bgfx::UniformType::Vec4);
    m_uShadowPenumbra = bgfx::createUniform("u_shadowPenumbra", bgfx::UniformType::Vec4);
    m_uShadowNormalBias = bgfx::createUniform("u_shadowNormalBias", bgfx::UniformType::Vec4);
    m_uShadowParams     = bgfx::createUniform("u_shadowParams",     bgfx::UniformType::Vec4);
    m_uShadowLightDir   = bgfx::createUniform("u_shadowLightDir",   bgfx::UniformType::Vec4);
    m_uShadowFilter     = bgfx::createUniform("u_shadowFilter",     bgfx::UniformType::Vec4);
    m_uShadowProjection = bgfx::createUniform("u_shadowProjection", bgfx::UniformType::Vec4);
    m_sShadowMaps[0] = bgfx::createUniform("s_shadowMap0", bgfx::UniformType::Sampler);
    m_sShadowMaps[1] = bgfx::createUniform("s_shadowMap1", bgfx::UniformType::Sampler);
    m_sShadowMaps[2] = bgfx::createUniform("s_shadowMap2", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(m_uLightViewProj) || !bgfx::isValid(m_uShadowParams)
        || !bgfx::isValid(m_uShadowLightDir) || !bgfx::isValid(m_uShadowFilter)
        || !bgfx::isValid(m_uShadowProjection) || !bgfx::isValid(m_uShadowCameraView)
        || !bgfx::isValid(m_uShadowCascadeSplits) || !bgfx::isValid(m_uShadowCascadeBlend)
        || !bgfx::isValid(m_uShadowPenumbra) || !bgfx::isValid(m_uShadowNormalBias)
        || !bgfx::isValid(m_sShadowMaps[0]) || !bgfx::isValid(m_sShadowMaps[1])
        || !bgfx::isValid(m_sShadowMaps[2])) {
        Debug::Logger::Error("Render", "shadow uniform creation failed");
        Shutdown();
        return false;
    }

    m_ready = true;
    return true;
}

void ShadowMap::Shutdown()
{
    for (bgfx::UniformHandle* u : {&m_uLightViewProj, &m_uShadowCameraView,
                                   &m_uShadowCascadeSplits, &m_uShadowCascadeBlend,
                                   &m_uShadowPenumbra, &m_uShadowNormalBias, &m_uShadowParams,
                                   &m_uShadowLightDir,
                                   &m_uShadowFilter, &m_uShadowProjection, &m_sShadowMaps[0],
                                   &m_sShadowMaps[1], &m_sShadowMaps[2]}) {
        if (bgfx::isValid(*u)) {
            bgfx::destroy(*u);
            *u = BGFX_INVALID_HANDLE;
        }
    }
    if (bgfx::isValid(m_program)) {
        bgfx::destroy(m_program);
        m_program = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_skinnedProgram)) {
        bgfx::destroy(m_skinnedProgram);
        m_skinnedProgram = BGFX_INVALID_HANDLE;
    }
    m_ready = false;
    m_attempted = false;
}

bool ShadowMap::CreateViewTarget(std::uint32_t resolution,
                                 bgfx::FrameBufferHandle& fb,
                                 bgfx::TextureHandle& tex) const
{
    fb = BGFX_INVALID_HANDLE;
    tex = BGFX_INVALID_HANDLE;

    const std::uint16_t res = static_cast<std::uint16_t>(resolution);
    if (res == 0) {
        return false;
    }

    // Color+depth RT: R32F color stores the post-divide depth the scene pass
    // samples. R32F gives full 32-bit float precision — zero quantization
    // artifacts on any scene size. POINT sampling is essential: the PCF in
    // fs_mesh compares each tap's depth against the fragment discretely, so the
    // sampler must return a stored occluder depth verbatim. Linear filtering
    // would blend neighbouring depths into an in-between value that no real
    // surface sits at, and thresholding that average produces the classic
    // striped self-shadow acne. D24S8 provides real depth testing for the
    // shadow pass's own geometry.
    const std::uint64_t colorFlags = BGFX_TEXTURE_RT
        | BGFX_SAMPLER_MIN_POINT
        | BGFX_SAMPLER_MAG_POINT
        | BGFX_SAMPLER_MIP_POINT
        | BGFX_SAMPLER_U_CLAMP
        | BGFX_SAMPLER_V_CLAMP;
    const std::uint64_t depthFlags = BGFX_TEXTURE_RT_WRITE_ONLY;

    bgfx::TextureHandle color = bgfx::createTexture2D(res, res, false, 1,
        bgfx::TextureFormat::R32F, colorFlags);
    bgfx::TextureHandle depth = bgfx::createTexture2D(res, res, false, 1,
        bgfx::TextureFormat::D24S8, depthFlags);
    if (!bgfx::isValid(color) || !bgfx::isValid(depth)) {
        Debug::Logger::Error("Render", "shadow texture creation failed at %ux%u", res, res);
        if (bgfx::isValid(color)) { bgfx::destroy(color); }
        if (bgfx::isValid(depth)) { bgfx::destroy(depth); }
        return false;
    }

    bgfx::TextureHandle attachments[2] = {color, depth};
    fb = bgfx::createFrameBuffer(2, attachments, false);
    if (!bgfx::isValid(fb)) {
        Debug::Logger::Error("Render", "shadow framebuffer creation failed at %ux%u", res, res);
        bgfx::destroy(color);
        bgfx::destroy(depth);
        return false;
    }

    // The color texture is what the scene pass samples; return it to the caller
    // so they can bind it through s_shadowMap.
    tex = color;
    return true;
}

void ShadowMap::DestroyViewTarget(bgfx::FrameBufferHandle& fb, bgfx::TextureHandle& tex) const
{
    // The RT owns two textures (R32F color + D24S8 depth) created with
    // `_destroyTextures = false`, so we must free both explicitly. The color
    // handle is `tex`; the depth handle is fetched from attachment slot 1.
    if (bgfx::isValid(fb)) {
        bgfx::TextureHandle depth = bgfx::getTexture(fb, 1);
        if (bgfx::isValid(depth)) {
            bgfx::destroy(depth);
        }
        bgfx::destroy(fb);
        fb = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(tex)) {
        bgfx::destroy(tex);
        tex = BGFX_INVALID_HANDLE;
    }
}

void ShadowMap::BeginDepthPass(RenderViewHandle shadowView, bgfx::FrameBufferHandle fb,
                               const ShadowConfig& config,
                               const float lightView[16], const float lightProj[16]) const
{
    const std::uint16_t res = static_cast<std::uint16_t>(config.resolution);
    bgfx::setViewMode(shadowView, bgfx::ViewMode::Sequential);
    // Clear color to white (R=1.0 => "far" depth so unshadowed texels read as
    // fully lit) and depth to 1.0. The R channel of the color attachment stores
    // the depth the scene pass samples.
    bgfx::setViewClear(shadowView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0xffffffff, 1.0f, 0);
    bgfx::setViewRect(shadowView, 0, 0, res, res);
    bgfx::setViewFrameBuffer(shadowView, fb);
    // `setViewTransform` populates the `u_viewProj` builtin vs_shadow reads, so
    // the CPU needs no explicit matrix upload to the depth-pass program.
    bgfx::setViewTransform(shadowView, lightView, lightProj);
}

void ShadowMap::BindForSampling(
                                const std::array<bgfx::TextureHandle, kShadowCascadeCount>& depthTextures,
                                 const std::array<std::array<float, 16>, kShadowCascadeCount>& lightViewProj,
                                 const float cameraView[16], const float lightDir[3],
                                 const std::array<float, kShadowCascadeCount>& splitDepths,
                                 const std::array<float, kShadowCascadeCount>& blendWidths,
                                 const std::array<float, kShadowCascadeCount>& penumbraScaleTexels,
                                 const std::array<float, kShadowCascadeCount>& normalBiasWorld,
                                 int casterIndex,
                                 const ShadowConfig& config) const
{
    // Point sampling: the shader does its own bilinear/PCSS comparison. Explicit
    // flags keep Vulkan's combined image-sampler complete (defaults can fail).
    constexpr std::uint64_t kShadowSampler = BGFX_SAMPLER_U_CLAMP
        | BGFX_SAMPLER_V_CLAMP
        | BGFX_SAMPLER_W_CLAMP
        | BGFX_SAMPLER_MIN_POINT
        | BGFX_SAMPLER_MAG_POINT
        | BGFX_SAMPLER_MIP_POINT;
    for (std::uint8_t cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
        bgfx::setTexture(static_cast<std::uint8_t>(4 + cascade), m_sShadowMaps[cascade],
                         depthTextures[cascade], kShadowSampler);
    }
    bgfx::setUniform(m_uLightViewProj, lightViewProj.data(), kShadowCascadeCount);
    bgfx::setUniform(m_uShadowCameraView, cameraView);
    const float splits[4] = {splitDepths[0], splitDepths[1], splitDepths[2], 0.0f};
    const float blends[4] = {blendWidths[0], blendWidths[1], blendWidths[2], 0.0f};
    const float penumbra[4] = {
        penumbraScaleTexels[0], penumbraScaleTexels[1], penumbraScaleTexels[2], 0.0f};
    const float normalBias[4] = {
        normalBiasWorld[0], normalBiasWorld[1], normalBiasWorld[2], 0.0f};
    bgfx::setUniform(m_uShadowCascadeSplits, splits);
    bgfx::setUniform(m_uShadowCascadeBlend, blends);
    bgfx::setUniform(m_uShadowPenumbra, penumbra);
    bgfx::setUniform(m_uShadowNormalBias, normalBias);
    const float params[4] = {
        config.depthBias,
        0.0f,
        1.0f / static_cast<float>(config.resolution),
        static_cast<float>(casterIndex + 1), // zero is disabled; otherwise index+1
    };
    bgfx::setUniform(m_uShadowParams, params);
    const float dir[4] = {lightDir[0], lightDir[1], lightDir[2], 0.0f};
    bgfx::setUniform(m_uShadowLightDir, dir);
    const float filter[4] = {
        config.blockerSearchRadiusTexels,
        config.minFilterRadiusTexels,
        config.maxFilterRadiusTexels,
        0.0f,
    };
    bgfx::setUniform(m_uShadowFilter, filter);
    const float projection[4] = {
        bgfx::getCaps()->homogeneousDepth ? 1.0f : 0.0f,
        0.0f, 0.0f, 0.0f,
    };
    bgfx::setUniform(m_uShadowProjection, projection);
}

void ShadowMap::BindDisabled(bgfx::TextureHandle whiteTexture) const
{
    constexpr std::uint64_t kShadowSampler = BGFX_SAMPLER_U_CLAMP
        | BGFX_SAMPLER_V_CLAMP
        | BGFX_SAMPLER_W_CLAMP
        | BGFX_SAMPLER_MIN_POINT
        | BGFX_SAMPLER_MAG_POINT
        | BGFX_SAMPLER_MIP_POINT;
    for (std::uint8_t cascade = 0; cascade < kShadowCascadeCount; ++cascade) {
        bgfx::setTexture(static_cast<std::uint8_t>(4 + cascade), m_sShadowMaps[cascade],
                         whiteTexture, kShadowSampler);
    }
    static const float kIdentity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    bgfx::setUniform(m_uLightViewProj, kIdentity);
    bgfx::setUniform(m_uShadowCameraView, kIdentity);
    const float cascades[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(m_uShadowCascadeSplits, cascades);
    bgfx::setUniform(m_uShadowCascadeBlend, cascades);
    bgfx::setUniform(m_uShadowPenumbra, cascades);
    bgfx::setUniform(m_uShadowNormalBias, cascades);
    const float params[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(m_uShadowParams, params);
    const float dir[4] = {0.0f, -1.0f, 0.0f, 0.0f};
    bgfx::setUniform(m_uShadowLightDir, dir);
    const float filter[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(m_uShadowFilter, filter);
    const float projection[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(m_uShadowProjection, projection);
}

} // namespace Concord
