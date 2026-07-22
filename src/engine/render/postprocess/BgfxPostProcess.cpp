#include "engine/render/postprocess/BgfxPostProcess.h"

#include "engine/debug/Logger.h"
#include "engine/render/shaders/generated/fs_fxaa.bin.h"
#include "engine/render/shaders/generated/vs_fullscreen.bin.h"

#include <bgfx/embedded_shader.h>

#include <cstdint>
#include <limits>

namespace {

const bgfx::EmbeddedShader kPresentShaders[] = {
    {
        "vs_fullscreen",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, vs_fullscreen)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    {
        "fs_fxaa",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, fs_fxaa)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    BGFX_EMBEDDED_SHADER_END()
};

/**
 * The FXAA fullscreen-pass edge-search strength. SMAA normally runs through
 * BgfxSmaa's independent edge, weight and neighborhood passes; the SMAA cases
 * here are defensive fallbacks for callers that route a preset to Present.
 */
float QualityFor(Concord::AntiAliasing mode) noexcept
{
    switch (mode) {
        case Concord::AntiAliasing::Fxaa:  return 1.0f;
        case Concord::AntiAliasing::Smaa2: return 2.0f;
        case Concord::AntiAliasing::Smaa4: return 3.0f;
        default:                           return 1.0f;
    }
}

} // namespace

namespace Concord {

bool BgfxPostProcess::EnsureReady()
{
    if (m_ready || m_attempted) {
        return m_ready;
    }
    m_attempted = true;

    const bgfx::RendererType::Enum type = bgfx::getRendererType();
    const bgfx::ShaderHandle vs = bgfx::createEmbeddedShader(kPresentShaders, type, "vs_fullscreen");
    const bgfx::ShaderHandle fs = bgfx::createEmbeddedShader(kPresentShaders, type, "fs_fxaa");
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
        Debug::Logger::Error("Render", "post-process shader creation failed");
        if (bgfx::isValid(vs)) {
            bgfx::destroy(vs);
        }
        if (bgfx::isValid(fs)) {
            bgfx::destroy(fs);
        }
        DestroyResources();
        return false;
    }
    m_program = bgfx::createProgram(vs, fs, true);
    if (!bgfx::isValid(m_program)) {
        Debug::Logger::Error("Render", "post-process program init failed");
        DestroyResources();
        return false;
    }

    m_sScene = bgfx::createUniform("s_scene", bgfx::UniformType::Sampler);
    m_sBloom = bgfx::createUniform("s_bloom", bgfx::UniformType::Sampler);
    m_uParams = bgfx::createUniform("u_presentParams", bgfx::UniformType::Vec4);
    m_uBloomComposite = bgfx::createUniform("u_bloomComposite", bgfx::UniformType::Vec4);
    m_uScreenShake = bgfx::createUniform("u_screenShake", bgfx::UniformType::Vec4);
    m_uMagnifier = bgfx::createUniform("u_magnifier", bgfx::UniformType::Vec4);
    m_uMagnifierParams = bgfx::createUniform("u_magnifierParams", bgfx::UniformType::Vec4);
    m_uLensFlare = bgfx::createUniform("u_lensFlare", bgfx::UniformType::Vec4);
    if (!bgfx::isValid(m_sScene) || !bgfx::isValid(m_sBloom)
        || !bgfx::isValid(m_uParams) || !bgfx::isValid(m_uBloomComposite)
        || !bgfx::isValid(m_uScreenShake) || !bgfx::isValid(m_uMagnifier)
        || !bgfx::isValid(m_uMagnifierParams) || !bgfx::isValid(m_uLensFlare)) {
        Debug::Logger::Error("Render", "post-process uniform creation failed");
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
        Debug::Logger::Error("Render", "post-process fullscreen buffer creation failed");
        DestroyResources();
        return false;
    }

    m_ready = true;
    return true;
}

void BgfxPostProcess::DestroyResources()
{
    if (bgfx::isValid(m_fullscreenVb)) {
        bgfx::destroy(m_fullscreenVb);
        m_fullscreenVb = BGFX_INVALID_HANDLE;
    }
    for (bgfx::UniformHandle* uniform : {
             &m_sScene, &m_sBloom, &m_uParams, &m_uBloomComposite,
             &m_uScreenShake, &m_uMagnifier, &m_uMagnifierParams, &m_uLensFlare}) {
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

void BgfxPostProcess::Shutdown()
{
    DestroyResources();
    m_attempted = false;
}

bool BgfxPostProcess::CreateTargets(std::uint32_t width, std::uint32_t height, Targets& out)
{
    DestroyTargets(out);
    if (width == 0 || height == 0
        || width > std::numeric_limits<std::uint16_t>::max()
        || height > std::numeric_limits<std::uint16_t>::max()) {
        Debug::Logger::Error("Render", "invalid post-process target size %ux%u", width, height);
        return false;
    }

    const std::uint16_t w = static_cast<std::uint16_t>(width);
    const std::uint16_t h = static_cast<std::uint16_t>(height);

    // RGBA16F (not RGBA8): lighting is tonemapped in the mesh shader but still
    // has sub-8-bit precision in float. The final present pass dithers into the
    // 8-bit swap chain — dithering into RGBA8 then MSAA/FXAA-filtering kills it
    // and bakes classic floor/wall isophote banding (see Anisoptera / FrostKiwi).
    const std::uint64_t colorFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    out.color = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA16F, colorFlags);
    // Sampleable depth (no WRITE_ONLY) so the ray-tracer SSR pass can read
    // rasterized scene depth for real-scene reflections.
    const std::uint64_t depthFlags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP
        | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
    out.depth = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::D24S8, depthFlags);
    if (!bgfx::isValid(out.color) || !bgfx::isValid(out.depth)) {
        Debug::Logger::Error("Render", "RGBA16F offscreen create failed — banding fix unavailable");
        DestroyTargets(out);
        return false;
    }
    Debug::Logger::Info("Render", "scene color RT %ux%u RGBA16F (HDR + final dither path)", width, height);

    // Own the textures ourselves (destroyTextures = false) so `color` stays
    // valid as a sampler source; DestroyTargets frees them explicitly.
    bgfx::TextureHandle attachments[2] = {out.color, out.depth};
    out.framebuffer = bgfx::createFrameBuffer(2, attachments, false);
    if (!bgfx::isValid(out.framebuffer)) {
        DestroyTargets(out);
        return false;
    }
    return true;
}

void BgfxPostProcess::DestroyTargets(Targets& targets)
{
    if (bgfx::isValid(targets.framebuffer)) {
        bgfx::destroy(targets.framebuffer);
        targets.framebuffer = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(targets.color)) {
        bgfx::destroy(targets.color);
        targets.color = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(targets.depth)) {
        bgfx::destroy(targets.depth);
        targets.depth = BGFX_INVALID_HANDLE;
    }
}

void BgfxPostProcess::Present(RenderViewHandle presentView, bgfx::TextureHandle color,
                              std::uint32_t width, std::uint32_t height, AntiAliasing mode,
                              bgfx::TextureHandle bloom, float bloomIntensity,
                              const ViewEffectState* effects)
{
    Draw(presentView, color, width, height, QualityFor(mode), bloom, bloomIntensity, effects);
}

void BgfxPostProcess::Blit(RenderViewHandle presentView, bgfx::TextureHandle color,
                           std::uint32_t width, std::uint32_t height,
                           bgfx::TextureHandle bloom, float bloomIntensity,
                           const ViewEffectState* effects)
{
    Draw(presentView, color, width, height, 0.0f, bloom, bloomIntensity, effects);
}

void BgfxPostProcess::Draw(RenderViewHandle presentView, bgfx::TextureHandle color,
                           std::uint32_t width, std::uint32_t height, float quality,
                           bgfx::TextureHandle bloom, float bloomIntensity,
                           const ViewEffectState* effects)
{
    if (!m_ready || presentView == kInvalidRenderView || !bgfx::isValid(color)) {
        return;
    }
    const std::uint16_t w = static_cast<std::uint16_t>(width);
    const std::uint16_t h = static_cast<std::uint16_t>(height);
    bgfx::setViewRect(presentView, 0, 0, w, h);

    // x: V flip when sampling the offscreen RT into the swap chain.
    // bgfx Vulkan/D3D use top-left origin on the window; offscreen colour
    // attachments are stored with the opposite V sense, so blitting without a
    // flip shows the world upside-down (floor on top — see user reports).
    // OpenGL (originBottomLeft) needs no flip. Match bgfx examples' screenSpaceQuad.
    // y,z: texel size for edge sampling; w: AA quality (0 = passthrough blit).
    const float flipV = bgfx::getCaps()->originBottomLeft ? 0.0f : 1.0f;
    const float params[4] = {
        flipV,
        1.0f / static_cast<float>(w),
        1.0f / static_cast<float>(h),
        quality,
    };
    bgfx::setUniform(m_uParams, params);

    // Bloom composite: when a valid bloom texture is supplied the present pass
    // adds it with `bloomIntensity`; otherwise the scene color doubles as a
    // harmless dummy binding and the intensity is forced to 0 (no-op add), so
    // the sampler is always bound (Vulkan requires it) without a black texture.
    const bool hasBloom = bgfx::isValid(bloom) && bloomIntensity > 0.0f;
    const float bloomParams[4] = {hasBloom ? bloomIntensity : 0.0f, 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(m_uBloomComposite, bloomParams);

    const ViewEffectState defaults{};
    const ViewEffectState& effectState = effects != nullptr ? *effects : defaults;
    const float shake[4] = {
        effectState.shakeOffsetPixels[0], effectState.shakeOffsetPixels[1], 0.0f, 0.0f};
    const float magnifier[4] = {
        effectState.magnifierCenter[0], effectState.magnifierCenter[1],
        effectState.magnifierRadius, effectState.magnifierZoom};
    const float magnifierParams[4] = {
        effectState.magnifierDistortion, effectState.magnifierFeather,
        effectState.magnifierEnabled != 0 ? 1.0f : 0.0f, 0.0f};
    bgfx::setUniform(m_uScreenShake, shake);
    bgfx::setUniform(m_uMagnifier, magnifier);
    bgfx::setUniform(m_uMagnifierParams, magnifierParams);
    // x,y: sun screen position (y-up); z: effective strength (0 disables).
    const bool flareOn = effectState.lensFlareEnabled != 0
        && effectState.lensFlareSunVisible != 0 && effectState.lensFlareIntensity > 0.0f;
    const float lensFlare[4] = {
        effectState.lensFlareSunPos[0], effectState.lensFlareSunPos[1],
        flareOn ? effectState.lensFlareIntensity : 0.0f, 0.0f};
    bgfx::setUniform(m_uLensFlare, lensFlare);
    bgfx::setTexture(0, m_sScene, color);
    bgfx::setTexture(1, m_sBloom, hasBloom ? bloom : color);

    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::setVertexBuffer(0, m_fullscreenVb);
    bgfx::submit(presentView, m_program);
}

} // namespace Concord
