#include "engine/render/postprocess/BgfxBloom.h"

#include "engine/debug/Logger.h"
#include "engine/render/shaders/generated/fs_bloom_down.bin.h"
#include "engine/render/shaders/generated/fs_bloom_up.bin.h"
#include "engine/render/shaders/generated/vs_bloom.bin.h"

#include <bgfx/embedded_shader.h>

#include <algorithm>
#include <cstdint>

namespace {

const bgfx::EmbeddedShader kBloomShaders[] = {
    {
        "vs_bloom",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, vs_bloom)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    {
        "fs_bloom_down",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, fs_bloom_down)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    {
        "fs_bloom_up",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, fs_bloom_up)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    BGFX_EMBEDDED_SHADER_END()
};

} // namespace

namespace Concord {

bool BgfxBloom::EnsureReady()
{
    if (m_ready || m_attempted) {
        return m_ready;
    }
    m_attempted = true;

    const bgfx::RendererType::Enum type = bgfx::getRendererType();
    const bgfx::ShaderHandle vs = bgfx::createEmbeddedShader(kBloomShaders, type, "vs_bloom");
    const bgfx::ShaderHandle fsDown = bgfx::createEmbeddedShader(kBloomShaders, type, "fs_bloom_down");
    const bgfx::ShaderHandle vs2 = bgfx::createEmbeddedShader(kBloomShaders, type, "vs_bloom");
    const bgfx::ShaderHandle fsUp = bgfx::createEmbeddedShader(kBloomShaders, type, "fs_bloom_up");
    if (!bgfx::isValid(vs) || !bgfx::isValid(fsDown) || !bgfx::isValid(vs2) || !bgfx::isValid(fsUp)) {
        Debug::Logger::Error("Render", "bloom shader creation failed");
        for (bgfx::ShaderHandle s : {vs, fsDown, vs2, fsUp}) {
            if (bgfx::isValid(s)) {
                bgfx::destroy(s);
            }
        }
        return false;
    }
    m_downProgram = bgfx::createProgram(vs, fsDown, true);
    m_upProgram = bgfx::createProgram(vs2, fsUp, true);
    if (!bgfx::isValid(m_downProgram) || !bgfx::isValid(m_upProgram)) {
        Debug::Logger::Error("Render", "bloom program init failed");
        DestroyResources();
        return false;
    }

    m_sTex = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
    m_uParams = bgfx::createUniform("u_bloomParams", bgfx::UniformType::Vec4);
    if (!bgfx::isValid(m_sTex) || !bgfx::isValid(m_uParams)) {
        Debug::Logger::Error("Render", "bloom uniform creation failed");
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
        Debug::Logger::Error("Render", "bloom fullscreen buffer creation failed");
        DestroyResources();
        return false;
    }

    m_ready = true;
    return true;
}

void BgfxBloom::DestroyResources()
{
    if (bgfx::isValid(m_fullscreenVb)) {
        bgfx::destroy(m_fullscreenVb);
        m_fullscreenVb = BGFX_INVALID_HANDLE;
    }
    for (bgfx::UniformHandle* u : {&m_sTex, &m_uParams}) {
        if (bgfx::isValid(*u)) {
            bgfx::destroy(*u);
            *u = BGFX_INVALID_HANDLE;
        }
    }
    for (bgfx::ProgramHandle* p : {&m_downProgram, &m_upProgram}) {
        if (bgfx::isValid(*p)) {
            bgfx::destroy(*p);
            *p = BGFX_INVALID_HANDLE;
        }
    }
    m_ready = false;
}

void BgfxBloom::Shutdown()
{
    DestroyResources();
    m_attempted = false;
}

bool BgfxBloom::CreateTargets(std::uint32_t fullWidth, std::uint32_t fullHeight, Targets& out)
{
    DestroyTargets(out);
    const std::uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

    std::uint32_t w = fullWidth;
    std::uint32_t h = fullHeight;
    for (std::uint32_t level = 0; level < kMaxMips; ++level) {
        w = std::max(1u, w / 2u);
        h = std::max(1u, h / 2u);
        Mip mip;
        mip.width = w;
        mip.height = h;
        mip.tex = bgfx::createTexture2D(static_cast<std::uint16_t>(w), static_cast<std::uint16_t>(h),
                                        false, 1, bgfx::TextureFormat::RGBA16F, flags);
        if (!bgfx::isValid(mip.tex)) {
            break;
        }
        mip.fb = bgfx::createFrameBuffer(1, &mip.tex, false);
        if (!bgfx::isValid(mip.fb)) {
            bgfx::destroy(mip.tex);
            break;
        }
        out.mips.push_back(mip);
        if (w <= 2 || h <= 2) {
            break;
        }
    }
    if (out.mips.empty()) {
        Debug::Logger::Warn("Render", "bloom mip chain create failed; bloom disabled for this view");
        return false;
    }
    out.result.width = out.mips.front().width;
    out.result.height = out.mips.front().height;
    out.result.tex = bgfx::createTexture2D(static_cast<std::uint16_t>(out.result.width),
                                           static_cast<std::uint16_t>(out.result.height),
                                           false, 1, bgfx::TextureFormat::RGBA16F, flags);
    if (bgfx::isValid(out.result.tex)) {
        out.result.fb = bgfx::createFrameBuffer(1, &out.result.tex, false);
    }
    if (!bgfx::isValid(out.result.tex) || !bgfx::isValid(out.result.fb)) {
        Debug::Logger::Warn("Render", "bloom result target create failed; bloom disabled for this view");
        DestroyTargets(out);
        return false;
    }
    return true;
}

void BgfxBloom::DestroyTargets(Targets& targets)
{
    for (Mip& mip : targets.mips) {
        if (bgfx::isValid(mip.fb)) {
            bgfx::destroy(mip.fb);
        }
        if (bgfx::isValid(mip.tex)) {
            bgfx::destroy(mip.tex);
        }
    }
    targets.mips.clear();
    if (bgfx::isValid(targets.result.fb)) {
        bgfx::destroy(targets.result.fb);
    }
    if (bgfx::isValid(targets.result.tex)) {
        bgfx::destroy(targets.result.tex);
    }
    targets.result = Mip{};
}

void BgfxBloom::DrawPass(RenderViewHandle view, bgfx::FrameBufferHandle target,
                         bgfx::TextureHandle source, bgfx::ProgramHandle program,
                         const float params[4], std::uint32_t width, std::uint32_t height, bool additive)
{
    bgfx::setViewFrameBuffer(view, target);
    bgfx::setViewRect(view, 0, 0, static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height));
    bgfx::setViewClear(view, BGFX_CLEAR_NONE);
    bgfx::setUniform(m_uParams, params);
    bgfx::setTexture(0, m_sTex, source);
    std::uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
    if (additive) {
        state |= BGFX_STATE_BLEND_ADD; // accumulate the smaller mip's blur
    }
    bgfx::setState(state);
    bgfx::setVertexBuffer(0, m_fullscreenVb);
    bgfx::submit(view, program);
}

bgfx::TextureHandle BgfxBloom::Generate(const RenderViewHandle* views, std::uint32_t viewCount,
                                        bgfx::TextureHandle sceneColor,
                                        std::uint32_t fullWidth, std::uint32_t fullHeight,
                                        const Targets& targets, float threshold, float filterRadius)
{
    if (!m_ready || !targets.Valid() || !bgfx::isValid(sceneColor) || views == nullptr) {
        return BGFX_INVALID_HANDLE;
    }
    const std::uint32_t mipCount = static_cast<std::uint32_t>(targets.mips.size());
    std::uint32_t v = 0;

    // Downsample: scene -> mip0 (with bright threshold), then mip i -> mip i+1.
    bgfx::TextureHandle src = sceneColor;
    std::uint32_t sw = fullWidth;
    std::uint32_t sh = fullHeight;
    for (std::uint32_t i = 0; i < mipCount && v < viewCount; ++i) {
        const float params[4] = {
            1.0f / static_cast<float>(sw),
            1.0f / static_cast<float>(sh),
            i == 0 ? 1.0f : 0.0f,
            threshold,
        };
        DrawPass(views[v++], targets.mips[i].fb, src, m_downProgram, params,
                 targets.mips[i].width, targets.mips[i].height, /*additive=*/false);
        src = targets.mips[i].tex;
        sw = targets.mips[i].width;
        sh = targets.mips[i].height;
    }

    // Upsample: additively accumulate mip i onto mip i-1, ending at mip 0.
    // Convert the user radius to source-texel UVs at each level. A fixed UV
    // radius made the top levels several pixels wide while barely filtering
    // the smallest levels, preserving recognizable reduced copies of bright
    // objects that looked like reflections instead of a continuous glow.
    for (std::uint32_t i = mipCount - 1; i > 0 && v < viewCount; --i) {
        const float params[4] = {
            filterRadius / static_cast<float>(targets.mips[i].width),
            filterRadius / static_cast<float>(targets.mips[i].height),
            0.0f,
            0.0f,
        };
        DrawPass(views[v++], targets.mips[i - 1].fb, targets.mips[i].tex, m_upProgram, params,
                 targets.mips[i - 1].width, targets.mips[i - 1].height, /*additive=*/true);
    }

    if (v >= viewCount) {
        return BGFX_INVALID_HANDLE;
    }
    const float finalParams[4] = {
        filterRadius / static_cast<float>(targets.mips.front().width),
        filterRadius / static_cast<float>(targets.mips.front().height),
        0.0f,
        0.0f,
    };
    DrawPass(views[v], targets.result.fb, targets.mips.front().tex, m_upProgram, finalParams,
             targets.result.width, targets.result.height, /*additive=*/false);
    return targets.result.tex;
}

} // namespace Concord
