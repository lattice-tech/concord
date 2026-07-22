#include "engine/render/debug/DebugTextOverlay.h"

#include "engine/debug/Logger.h"
#include "engine/render/shaders/generated/fs_debugtext.bin.h"
#include "engine/render/shaders/generated/vs_debugtext.bin.h"

#include <bgfx/embedded_shader.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace {

const bgfx::EmbeddedShader kDebugTextShaders[] = {
    {
        "vs_debugtext",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, vs_debugtext)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    {
        "fs_debugtext",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, fs_debugtext)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    BGFX_EMBEDDED_SHADER_END()
};

struct DebugVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
    std::uint32_t abgr = 0xffffffffu;
};

std::uint32_t ToAbgr(std::uint32_t rgba) noexcept
{
    const std::uint32_t r = (rgba >> 24) & 0xffu;
    const std::uint32_t g = (rgba >> 16) & 0xffu;
    const std::uint32_t b = (rgba >> 8) & 0xffu;
    const std::uint32_t a = rgba & 0xffu;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

/**
 * Pushes one textured glyph quad. Inputs are a pixel-space rect (top-left
 * origin, y down) and the atlas UVs; the rect is converted to NDC here so the
 * vertex shader can pass positions straight through.
 */
void PushGlyphQuad(std::vector<DebugVertex>& verts, const Concord::FontGlyphQuad& q,
                   float shiftX, float shiftY, float w, float h, std::uint32_t abgr)
{
    const float x0 = q.x0 + shiftX;
    const float x1 = q.x1 + shiftX;
    const float y0 = q.y0 + shiftY;
    const float y1 = q.y1 + shiftY;
    const float nx0 = (x0 / w) * 2.0f - 1.0f;
    const float nx1 = (x1 / w) * 2.0f - 1.0f;
    const float ny0 = 1.0f - (y0 / h) * 2.0f; // top
    const float ny1 = 1.0f - (y1 / h) * 2.0f; // bottom

    verts.push_back({nx0, ny0, 0.0f, q.s0, q.t0, abgr});
    verts.push_back({nx1, ny0, 0.0f, q.s1, q.t0, abgr});
    verts.push_back({nx1, ny1, 0.0f, q.s1, q.t1, abgr});
    verts.push_back({nx0, ny0, 0.0f, q.s0, q.t0, abgr});
    verts.push_back({nx1, ny1, 0.0f, q.s1, q.t1, abgr});
    verts.push_back({nx0, ny1, 0.0f, q.s0, q.t1, abgr});
}

/**
 * Map a byte stream to printable ASCII. Skips UTF-8 continuation / multi-byte
 * lead bytes so an em-dash does not become "???" (three replacement marks).
 */
char NextAsciiGlyph(const std::string& text, std::size_t& i)
{
    if (i >= text.size()) {
        return '\0';
    }
    const auto c = static_cast<unsigned char>(text[i++]);
    if (c < 0x80u) {
        return (c < 32u) ? '?' : static_cast<char>(c);
    }
    // UTF-8 lead: skip the rest of the sequence, emit a simple ASCII stand-in.
    if ((c & 0xE0u) == 0xC0u) {
        if (i < text.size()) {
            ++i;
        }
    } else if ((c & 0xF0u) == 0xE0u) {
        i = std::min(i + 2u, text.size());
    } else if ((c & 0xF8u) == 0xF0u) {
        i = std::min(i + 3u, text.size());
    }
    return '-';
}

} // namespace

namespace Concord {

bool DebugTextOverlay::EnsureReady()
{
    if (m_ready) {
        return true;
    }

    if (!m_atlas.EnsureReady()) {
        return false;
    }

    const bgfx::RendererType::Enum type = bgfx::getRendererType();
    const bgfx::ShaderHandle vs = bgfx::createEmbeddedShader(kDebugTextShaders, type, "vs_debugtext");
    const bgfx::ShaderHandle fs = bgfx::createEmbeddedShader(kDebugTextShaders, type, "fs_debugtext");
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs)) {
        Debug::Logger::Error("Render", "debug text overlay shader creation failed");
        if (bgfx::isValid(vs)) {
            bgfx::destroy(vs);
        }
        if (bgfx::isValid(fs)) {
            bgfx::destroy(fs);
        }
        return false;
    }

    m_program = bgfx::createProgram(vs, fs, true);
    if (!bgfx::isValid(m_program)) {
        Debug::Logger::Error("Render", "debug text overlay program init failed");
        return false;
    }

    m_sFont = bgfx::createUniform("s_font", bgfx::UniformType::Sampler);
    if (!bgfx::isValid(m_sFont)) {
        Debug::Logger::Error("Render", "debug text overlay sampler init failed");
        return false;
    }

    m_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    m_ready = true;
    return true;
}

void DebugTextOverlay::Shutdown()
{
    if (bgfx::isValid(m_sFont)) {
        bgfx::destroy(m_sFont);
        m_sFont = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_program)) {
        bgfx::destroy(m_program);
        m_program = BGFX_INVALID_HANDLE;
    }
    m_atlas.Shutdown();
    m_ready = false;
}

float DebugTextOverlay::MeasureLineWidth(const std::string& text)
{
    float pen = 0.0f;
    std::size_t i = 0;
    FontGlyphQuad q;
    while (i < text.size()) {
        const char ch = NextAsciiGlyph(text, i);
        if (ch == '\0') {
            break;
        }
        m_atlas.GetGlyph(ch, pen, 0.0f, q);
    }
    return pen;
}

void DebugTextOverlay::Draw(RenderViewHandle view, std::uint32_t width, std::uint32_t height,
                            const std::vector<DebugTextLine>& lines, Anchor anchor)
{
    if (!m_ready || lines.empty() || width == 0 || height == 0
        || view == kInvalidRenderView || !bgfx::isValid(m_program)) {
        return;
    }

    constexpr float kPadX = 12.0f;
    constexpr float kPadY = 12.0f;
    constexpr float kShadow = 1.5f; // soft drop shadow for legibility
    constexpr std::uint32_t kShadowAbgr = 0xE6000000u;

    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float lineStep = m_atlas.LineHeight();

    std::vector<DebugVertex> verts;
    verts.reserve(lines.size() * 96u * 12u); // glyph quad + shadow quad

    if (anchor == Anchor::TopRight) {
        // Right-aligned, stacking downward from the top-right for a persistent HUD.
        float baseline = kPadY + m_atlas.Ascent();
        for (const DebugTextLine& line : lines) {
            if (baseline + m_atlas.Descent() > h) {
                break;
            }
            const std::uint32_t abgr = ToAbgr(line.color | 0x000000ffu);
            const float lineWidth = MeasureLineWidth(line.text);
            float penX = std::max(kPadX, w - kPadX - lineWidth);
            std::size_t i = 0;
            while (i < line.text.size()) {
                const char ch = NextAsciiGlyph(line.text, i);
                if (ch == '\0') {
                    break;
                }
                FontGlyphQuad q;
                if (m_atlas.GetGlyph(ch, penX, baseline, q)) {
                    PushGlyphQuad(verts, q, kShadow, kShadow, w, h, kShadowAbgr);
                    PushGlyphQuad(verts, q, 0.0f, 0.0f, w, h, abgr);
                }
            }
            baseline += lineStep;
        }
    } else {
        // Stack upward from the bottom-left (newest line at the bottom).
        float baseline = h - kPadY - m_atlas.Descent();
        for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
            if (baseline - m_atlas.Ascent() < 0.0f) {
                break;
            }
            const std::uint32_t abgr = ToAbgr(it->color | 0x000000ffu);
            float penX = kPadX;
            std::size_t i = 0;
            while (i < it->text.size()) {
                const char ch = NextAsciiGlyph(it->text, i);
                if (ch == '\0') {
                    break;
                }
                FontGlyphQuad q;
                if (m_atlas.GetGlyph(ch, penX, baseline, q)) {
                    PushGlyphQuad(verts, q, kShadow, kShadow, w, h, kShadowAbgr);
                    PushGlyphQuad(verts, q, 0.0f, 0.0f, w, h, abgr);
                }
                if (penX > w - kPadX) {
                    break;
                }
            }
            baseline -= lineStep;
        }
    }

    if (verts.empty()) {
        return;
    }

    const std::uint32_t maxVerts = static_cast<std::uint32_t>(verts.size());
    if (bgfx::getAvailTransientVertexBuffer(maxVerts, m_layout) < maxVerts) {
        return;
    }

    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, maxVerts, m_layout);
    std::memcpy(tvb.data, verts.data(), sizeof(DebugVertex) * maxVerts);

    bgfx::setViewRect(view, 0, 0, static_cast<std::uint16_t>(width),
                      static_cast<std::uint16_t>(height));
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                   | BGFX_STATE_BLEND_ALPHA
                   | BGFX_STATE_DEPTH_TEST_ALWAYS);
    bgfx::setTexture(0, m_sFont, m_atlas.Texture());
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::submit(view, m_program);
}

} // namespace Concord
