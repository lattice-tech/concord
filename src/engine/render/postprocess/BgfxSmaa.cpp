#include "engine/render/postprocess/BgfxSmaa.h"

#include "engine/debug/Logger.h"
#include "engine/render/postprocess/smaa/SmaaAreaTex.h"
#include "engine/render/postprocess/smaa/SmaaSearchTex.h"
#include "engine/render/shaders/generated/fs_smaa_blend.bin.h"
#include "engine/render/shaders/generated/fs_smaa_edges.bin.h"
#include "engine/render/shaders/generated/fs_smaa_weights.bin.h"
#include "engine/render/shaders/generated/vs_fullscreen.bin.h"

#include <bgfx/embedded_shader.h>

#include <cstdint>
#include <vector>

namespace {

const bgfx::EmbeddedShader kSmaaShaders[] = {
    {
        "vs_fullscreen",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, vs_fullscreen)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    {
        "fs_smaa_edges",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, fs_smaa_edges)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    {
        "fs_smaa_weights",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, fs_smaa_weights)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    {
        "fs_smaa_blend",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, fs_smaa_blend)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    BGFX_EMBEDDED_SHADER_END()
};

/** Creates a render target sampled with clamping. Returns false on failure. */
bool CreateRt(std::uint16_t w, std::uint16_t h, bgfx::TextureFormat::Enum format,
              bgfx::TextureHandle& tex, bgfx::FrameBufferHandle& fb)
{
    const std::uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    tex = bgfx::createTexture2D(w, h, false, 1, format, flags);
    if (!bgfx::isValid(tex)) {
        return false;
    }
    bgfx::TextureHandle attachments[1] = {tex};
    fb = bgfx::createFrameBuffer(1, attachments, false);
    return bgfx::isValid(fb);
}

/** Uploads immutable lookup data with the linear-clamp sampling SMAA requires. */
bgfx::TextureHandle CreateLookupTexture(std::uint16_t width, std::uint16_t height,
                                        bgfx::TextureFormat::Enum format,
                                        const std::vector<std::uint8_t>& bytes)
{
    const std::uint64_t flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP
        | BGFX_SAMPLER_MIP_POINT;
    return bgfx::createTexture2D(width, height, false, 1, format, flags,
                                 bgfx::copy(bytes.data(), static_cast<std::uint32_t>(bytes.size())));
}

} // namespace

namespace Concord {

bool BgfxSmaa::EnsureReady()
{
    if (m_ready || m_attempted) {
        return m_ready;
    }
    m_attempted = true;

    const bgfx::RendererType::Enum type = bgfx::getRendererType();
    bgfx::ShaderHandle vs = bgfx::createEmbeddedShader(kSmaaShaders, type, "vs_fullscreen");
    bgfx::ShaderHandle fsEdges = bgfx::createEmbeddedShader(kSmaaShaders, type, "fs_smaa_edges");
    bgfx::ShaderHandle fsWeights = bgfx::createEmbeddedShader(kSmaaShaders, type, "fs_smaa_weights");
    bgfx::ShaderHandle fsBlend = bgfx::createEmbeddedShader(kSmaaShaders, type, "fs_smaa_blend");
    if (!bgfx::isValid(vs) || !bgfx::isValid(fsEdges)
        || !bgfx::isValid(fsWeights) || !bgfx::isValid(fsBlend)) {
        Debug::Logger::Error("Render", "SMAA shader creation failed");
        for (bgfx::ShaderHandle shader : {vs, fsEdges, fsWeights, fsBlend}) {
            if (bgfx::isValid(shader)) {
                bgfx::destroy(shader);
            }
        }
        DestroyResources();
        return false;
    }

    m_edgesProgram = bgfx::createProgram(vs, fsEdges, false);
    m_weightsProgram = bgfx::createProgram(vs, fsWeights, false);
    m_blendProgram = bgfx::createProgram(vs, fsBlend, false);
    for (bgfx::ShaderHandle shader : {vs, fsEdges, fsWeights, fsBlend}) {
        bgfx::destroy(shader);
    }
    if (!bgfx::isValid(m_edgesProgram) || !bgfx::isValid(m_weightsProgram)
        || !bgfx::isValid(m_blendProgram)) {
        Debug::Logger::Error("Render", "SMAA program init failed");
        DestroyResources();
        return false;
    }

    m_sColor = bgfx::createUniform("s_color", bgfx::UniformType::Sampler);
    m_sEdges = bgfx::createUniform("s_edges", bgfx::UniformType::Sampler);
    m_sWeights = bgfx::createUniform("s_weights", bgfx::UniformType::Sampler);
    m_sArea = bgfx::createUniform("s_area", bgfx::UniformType::Sampler);
    m_sSearch = bgfx::createUniform("s_search", bgfx::UniformType::Sampler);
    m_uTexel = bgfx::createUniform("u_smaaTexel", bgfx::UniformType::Vec4);
    m_uConfig = bgfx::createUniform("u_smaaConfig", bgfx::UniformType::Vec4);
    m_uPresentParams = bgfx::createUniform("u_presentParams", bgfx::UniformType::Vec4);
    for (bgfx::UniformHandle uniform : {
             m_sColor, m_sEdges, m_sWeights, m_sArea, m_sSearch,
             m_uTexel, m_uConfig, m_uPresentParams}) {
        if (!bgfx::isValid(uniform)) {
            Debug::Logger::Error("Render", "SMAA uniform creation failed");
            DestroyResources();
            return false;
        }
    }

    std::vector<std::uint8_t> areaBytes;
    std::vector<std::uint8_t> searchBytes;
    Smaa::BuildAreaTex(areaBytes);
    Smaa::BuildSearchTex(searchBytes);
    m_areaTexture = CreateLookupTexture(Smaa::kAreaTexWidth, Smaa::kAreaTexHeight,
                                        bgfx::TextureFormat::RG8, areaBytes);
    m_searchTexture = CreateLookupTexture(Smaa::kSearchTexWidth, Smaa::kSearchTexHeight,
                                          bgfx::TextureFormat::R8, searchBytes);
    if (!bgfx::isValid(m_areaTexture) || !bgfx::isValid(m_searchTexture)) {
        Debug::Logger::Error("Render", "SMAA lookup texture creation failed");
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
        Debug::Logger::Error("Render", "SMAA fullscreen buffer creation failed");
        DestroyResources();
        return false;
    }

    m_ready = true;
    return true;
}

void BgfxSmaa::DestroyResources()
{
    for (bgfx::ProgramHandle* program : {&m_edgesProgram, &m_weightsProgram, &m_blendProgram}) {
        if (bgfx::isValid(*program)) {
            bgfx::destroy(*program);
            *program = BGFX_INVALID_HANDLE;
        }
    }
    for (bgfx::UniformHandle* uniform : {
             &m_sColor, &m_sEdges, &m_sWeights, &m_sArea, &m_sSearch,
             &m_uTexel, &m_uConfig, &m_uPresentParams}) {
        if (bgfx::isValid(*uniform)) {
            bgfx::destroy(*uniform);
            *uniform = BGFX_INVALID_HANDLE;
        }
    }
    for (bgfx::TextureHandle* texture : {&m_areaTexture, &m_searchTexture}) {
        if (bgfx::isValid(*texture)) {
            bgfx::destroy(*texture);
            *texture = BGFX_INVALID_HANDLE;
        }
    }
    if (bgfx::isValid(m_fullscreenVb)) {
        bgfx::destroy(m_fullscreenVb);
        m_fullscreenVb = BGFX_INVALID_HANDLE;
    }
    m_ready = false;
}

void BgfxSmaa::Shutdown()
{
    for (auto& [key, targets] : m_targets) {
        DestroyTargets(targets);
    }
    m_targets.clear();
    DestroyResources();
    m_attempted = false;
}

BgfxSmaa::Targets* BgfxSmaa::EnsureTargets(RenderViewHandle key, std::uint32_t width, std::uint32_t height)
{
    Targets& targets = m_targets[key];
    if (targets.Matches(width, height)) {
        return &targets;
    }
    DestroyTargets(targets);

    const std::uint16_t w = static_cast<std::uint16_t>(width);
    const std::uint16_t h = static_cast<std::uint16_t>(height);
    if (!CreateRt(w, h, bgfx::TextureFormat::RGBA8, targets.edges, targets.edgesFb)
        || !CreateRt(w, h, bgfx::TextureFormat::RGBA8, targets.weights, targets.weightsFb)
        // Preserve the scene's sub-8-bit precision until the final dithered
        // present. Quantizing the blended image here reintroduces banding.
        || !CreateRt(w, h, bgfx::TextureFormat::RGBA16F, targets.result, targets.resultFb)) {
        DestroyTargets(targets);
        return nullptr;
    }
    targets.width = width;
    targets.height = height;
    return &targets;
}

void BgfxSmaa::DestroyTargets(Targets& targets)
{
    for (bgfx::FrameBufferHandle* fb : {&targets.edgesFb, &targets.weightsFb, &targets.resultFb}) {
        if (bgfx::isValid(*fb)) {
            bgfx::destroy(*fb);
            *fb = BGFX_INVALID_HANDLE;
        }
    }
    for (bgfx::TextureHandle* t : {&targets.edges, &targets.weights, &targets.result}) {
        if (bgfx::isValid(*t)) {
            bgfx::destroy(*t);
            *t = BGFX_INVALID_HANDLE;
        }
    }
    targets.width = 0;
    targets.height = 0;
}

bgfx::TextureHandle BgfxSmaa::Run(RenderViewHandle key,
                                  RenderViewHandle edgeView, RenderViewHandle weightView, RenderViewHandle blendView,
                                  bgfx::TextureHandle sceneColor, std::uint32_t width, std::uint32_t height,
                                  AntiAliasing mode)
{
    if (!m_ready || !bgfx::isValid(sceneColor)) {
        return BGFX_INVALID_HANDLE;
    }
    Targets* targets = EnsureTargets(key, width, height);
    if (targets == nullptr) {
        return BGFX_INVALID_HANDLE;
    }

    const std::uint16_t w = static_cast<std::uint16_t>(width);
    const std::uint16_t h = static_cast<std::uint16_t>(height);
    const float texel[4] = {
        1.0f / static_cast<float>(w), 1.0f / static_cast<float>(h),
        static_cast<float>(w), static_cast<float>(h),
    };
    const float highConfig[4] = {0.10f, 16.0f, 8.0f, 0.25f};
    const float ultraConfig[4] = {0.05f, 32.0f, 16.0f, 0.25f};
    const float* config = mode == AntiAliasing::Smaa4 ? ultraConfig : highConfig;
    const float noFlip[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float resolveFlip[4] = {
        bgfx::getCaps()->originBottomLeft ? 0.0f : 1.0f,
        0.0f, 0.0f, 0.0f,
    };
    const std::uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;

    // Pass 1: edges = detect(sceneColor).
    bgfx::setViewFrameBuffer(edgeView, targets->edgesFb);
    bgfx::setViewClear(edgeView, BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);
    bgfx::setViewRect(edgeView, 0, 0, w, h);
    bgfx::setUniform(m_uPresentParams, noFlip);
    bgfx::setUniform(m_uTexel, texel);
    bgfx::setUniform(m_uConfig, config);
    bgfx::setTexture(0, m_sColor, sceneColor);
    bgfx::setState(state);
    bgfx::setVertexBuffer(0, m_fullscreenVb);
    bgfx::submit(edgeView, m_edgesProgram);

    // Pass 2: weights = calculate(edges).
    bgfx::setViewFrameBuffer(weightView, targets->weightsFb);
    bgfx::setViewClear(weightView, BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);
    bgfx::setViewRect(weightView, 0, 0, w, h);
    bgfx::setUniform(m_uPresentParams, noFlip);
    bgfx::setUniform(m_uTexel, texel);
    bgfx::setUniform(m_uConfig, config);
    bgfx::setTexture(0, m_sEdges, targets->edges);
    bgfx::setTexture(1, m_sArea, m_areaTexture);
    bgfx::setTexture(2, m_sSearch, m_searchTexture);
    bgfx::setState(state);
    bgfx::setVertexBuffer(0, m_fullscreenVb);
    bgfx::submit(weightView, m_weightsProgram);

    // Pass 3: result = blend(sceneColor, weights). The first two no-flip
    // offscreen passes cancel each other's render-target orientation change;
    // this third pass must flip its input so the result keeps the same texture
    // orientation as sceneColor. The final present then performs the one
    // expected offscreen-to-window flip instead of displaying the scene upside down.
    bgfx::setViewFrameBuffer(blendView, targets->resultFb);
    bgfx::setViewClear(blendView, BGFX_CLEAR_COLOR, 0x00000000, 1.0f, 0);
    bgfx::setViewRect(blendView, 0, 0, w, h);
    bgfx::setUniform(m_uPresentParams, resolveFlip);
    bgfx::setUniform(m_uTexel, texel);
    bgfx::setTexture(0, m_sColor, sceneColor);
    bgfx::setTexture(1, m_sWeights, targets->weights);
    bgfx::setState(state);
    bgfx::setVertexBuffer(0, m_fullscreenVb);
    bgfx::submit(blendView, m_blendProgram);

    return targets->result;
}

void BgfxSmaa::Release(RenderViewHandle key)
{
    const auto it = m_targets.find(key);
    if (it != m_targets.end()) {
        DestroyTargets(it->second);
        m_targets.erase(it);
    }
}

} // namespace Concord
