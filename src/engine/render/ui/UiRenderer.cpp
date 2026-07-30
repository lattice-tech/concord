#include "engine/render/ui/UiRenderer.h"

#include "engine/debug/Logger.h"
#include "engine/render/shaders/generated/fs_debugtext.bin.h"
#include "engine/render/shaders/generated/fs_ui_image.bin.h"
#include "engine/render/shaders/generated/vs_debugtext.bin.h"

#include <bgfx/embedded_shader.h>

#include <algorithm>
#include <cstring>
#include <iterator>

namespace {

const bgfx::EmbeddedShader kUiShaders[] = {
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
    {
        "fs_ui_image",
        {
            BGFX_EMBEDDED_SHADER_SPIRV(bgfx::RendererType::Vulkan, fs_ui_image)
            { bgfx::RendererType::Count, nullptr, 0 },
        },
    },
    BGFX_EMBEDDED_SHADER_END()
};

/** Repacks 0xRRGGBBAA (engine convention) into the 0xAABBGGRR bgfx vertex color. */
std::uint32_t ToAbgr(std::uint32_t rgba) noexcept
{
    const std::uint32_t r = (rgba >> 24) & 0xffu;
    const std::uint32_t g = (rgba >> 16) & 0xffu;
    const std::uint32_t b = (rgba >> 8) & 0xffu;
    const std::uint32_t a = rgba & 0xffu;
    return (a << 24) | (b << 16) | (g << 8) | r;
}

/**
 * Maps a byte stream to printable ASCII, skipping UTF-8 multi-byte sequences so
 * non-ASCII does not emit stray glyphs (mirrors the debug text overlay).
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
    if ((c & 0xE0u) == 0xC0u) {
        if (i < text.size()) { ++i; }
    } else if ((c & 0xF0u) == 0xE0u) {
        i = (i + 2u < text.size()) ? i + 2u : text.size();
    } else if ((c & 0xF8u) == 0xF0u) {
        i = (i + 3u < text.size()) ? i + 3u : text.size();
    }
    return '-';
}

} // namespace

namespace Concord {

bool UiRenderer::EnsureReady()
{
    if (m_ready) {
        return true;
    }
    if (!m_atlas.EnsureReady()) {
        return false;
    }

    const bgfx::RendererType::Enum type = bgfx::getRendererType();
    const bgfx::ShaderHandle vs = bgfx::createEmbeddedShader(kUiShaders, type, "vs_debugtext");
    const bgfx::ShaderHandle fs = bgfx::createEmbeddedShader(kUiShaders, type, "fs_debugtext");
    // The image path needs its own vertex shader instance: createProgram takes
    // ownership of the shaders it destroys, so sharing one handle across two
    // programs would double-free it.
    const bgfx::ShaderHandle imageVs =
        bgfx::createEmbeddedShader(kUiShaders, type, "vs_debugtext");
    const bgfx::ShaderHandle imageFs =
        bgfx::createEmbeddedShader(kUiShaders, type, "fs_ui_image");
    if (!bgfx::isValid(vs) || !bgfx::isValid(fs) || !bgfx::isValid(imageVs)
        || !bgfx::isValid(imageFs)) {
        Debug::Logger::Error("Render", "UI renderer shader creation failed");
        for (bgfx::ShaderHandle handle : {vs, fs, imageVs, imageFs}) {
            if (bgfx::isValid(handle)) { bgfx::destroy(handle); }
        }
        return false;
    }
    m_program = bgfx::createProgram(vs, fs, true);
    m_imageProgram = bgfx::createProgram(imageVs, imageFs, true);
    m_sTex = bgfx::createUniform("s_font", bgfx::UniformType::Sampler);

    // 1x1 opaque white, single channel: coverage 1 so solid rects render as the
    // pure vertex color through the shared (coverage * color) fragment shader.
    const std::uint8_t whitePixel = 0xff;
    m_white = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::R8,
                                    BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
                                    bgfx::copy(&whitePixel, sizeof(whitePixel)));

    m_layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    m_ready = bgfx::isValid(m_program) && bgfx::isValid(m_imageProgram)
        && bgfx::isValid(m_sTex) && bgfx::isValid(m_white);
    if (!m_ready) {
        Debug::Logger::Error("Render", "UI renderer resource init failed");
        Shutdown();
    }
    return m_ready;
}

void UiRenderer::Shutdown()
{
    if (bgfx::isValid(m_white)) { bgfx::destroy(m_white); m_white = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(m_sTex)) { bgfx::destroy(m_sTex); m_sTex = BGFX_INVALID_HANDLE; }
    if (bgfx::isValid(m_imageProgram)) {
        bgfx::destroy(m_imageProgram);
        m_imageProgram = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(m_program)) { bgfx::destroy(m_program); m_program = BGFX_INVALID_HANDLE; }
    m_atlas.Shutdown();
    m_ready = false;
}

void UiRenderer::PushQuad(std::vector<UiVertex>& out, float x0, float y0, float x1, float y1,
                          float u0, float v0, float u1, float v1, std::uint32_t abgr,
                          float w, float h)
{
    // Pixel space (top-left origin) -> NDC; the vertex shader passes xy through.
    const float nx0 = (x0 / w) * 2.0f - 1.0f;
    const float nx1 = (x1 / w) * 2.0f - 1.0f;
    const float ny0 = 1.0f - (y0 / h) * 2.0f;
    const float ny1 = 1.0f - (y1 / h) * 2.0f;
    out.push_back({nx0, ny0, 0.0f, u0, v0, abgr});
    out.push_back({nx1, ny0, 0.0f, u1, v0, abgr});
    out.push_back({nx1, ny1, 0.0f, u1, v1, abgr});
    out.push_back({nx0, ny0, 0.0f, u0, v0, abgr});
    out.push_back({nx1, ny1, 0.0f, u1, v1, abgr});
    out.push_back({nx0, ny1, 0.0f, u0, v1, abgr});
}

void UiRenderer::PushTriangle(std::vector<UiVertex>& out,
                              float x0, float y0, float x1, float y1, float x2, float y2,
                              std::uint32_t abgr, float w, float h)
{
    const float nx0 = (x0 / w) * 2.0f - 1.0f;
    const float nx1 = (x1 / w) * 2.0f - 1.0f;
    const float nx2 = (x2 / w) * 2.0f - 1.0f;
    const float ny0 = 1.0f - (y0 / h) * 2.0f;
    const float ny1 = 1.0f - (y1 / h) * 2.0f;
    const float ny2 = 1.0f - (y2 / h) * 2.0f;
    out.push_back({nx0, ny0, 0.0f, 0.0f, 0.0f, abgr});
    out.push_back({nx1, ny1, 0.0f, 0.0f, 0.0f, abgr});
    out.push_back({nx2, ny2, 0.0f, 0.0f, 0.0f, abgr});
}

void UiRenderer::PushRoundedRect(std::vector<UiVertex>& out, const UI::DrawCommand& command,
                                 float w, float h)
{
    const UI::Rect& r = command.rect;
    const float radius = std::min(command.cornerRadius,
                                  std::min(r.width, r.height) * 0.5f);
    if (!(radius > 0.0f)) {
        PushQuad(out, r.x, r.y, r.x + r.width, r.y + r.height,
                 0.0f, 0.0f, 1.0f, 1.0f, ToAbgr(command.color), w, h);
    } else {
        const std::uint32_t abgr = ToAbgr(command.color);
        const float x0 = r.x;
        const float y0 = r.y;
        const float x1 = r.x + r.width;
        const float y1 = r.y + r.height;
        PushQuad(out, x0 + radius, y0, x1 - radius, y1,
                 0.0f, 0.0f, 1.0f, 1.0f, abgr, w, h);
        PushQuad(out, x0, y0 + radius, x0 + radius, y1 - radius,
                 0.0f, 0.0f, 1.0f, 1.0f, abgr, w, h);
        PushQuad(out, x1 - radius, y0 + radius, x1, y1 - radius,
                 0.0f, 0.0f, 1.0f, 1.0f, abgr, w, h);

        constexpr int segments = 8;
        const auto corner = [&](float cx, float cy, float startAngle) {
            for (int index = 0; index < segments; ++index) {
                const float a0 = startAngle + (index / static_cast<float>(segments)) * 1.57079637f;
                const float a1 = startAngle + ((index + 1) / static_cast<float>(segments)) * 1.57079637f;
                PushTriangle(out, cx, cy,
                             cx + std::cos(a0) * radius, cy + std::sin(a0) * radius,
                             cx + std::cos(a1) * radius, cy + std::sin(a1) * radius,
                             abgr, w, h);
            }
        };
        corner(x0 + radius, y0 + radius, 3.14159274f);
        corner(x1 - radius, y0 + radius, 4.71238899f);
        corner(x1 - radius, y1 - radius, 0.0f);
        corner(x0 + radius, y1 - radius, 1.57079637f);
    }

    if (command.borderThickness > 0.0f && (command.borderColor & 0xffu) != 0u) {
        const std::uint32_t border = ToAbgr(command.borderColor);
        const float t = command.borderThickness;
        PushQuad(out, r.x, r.y, r.x + r.width, r.y + t,
                 0.0f, 0.0f, 1.0f, 1.0f, border, w, h);
        PushQuad(out, r.x, r.y + r.height - t, r.x + r.width, r.y + r.height,
                 0.0f, 0.0f, 1.0f, 1.0f, border, w, h);
        PushQuad(out, r.x, r.y + t, r.x + t, r.y + r.height - t,
                 0.0f, 0.0f, 1.0f, 1.0f, border, w, h);
        PushQuad(out, r.x + r.width - t, r.y + t, r.x + r.width, r.y + r.height - t,
                 0.0f, 0.0f, 1.0f, 1.0f, border, w, h);
    }
}

float UiRenderer::MeasureText(const std::string& text, float scale)
{
    float pen = 0.0f;
    std::size_t i = 0;
    FontGlyphQuad q;
    while (i < text.size()) {
        const char ch = NextAsciiGlyph(text, i);
        if (ch == '\0') { break; }
        m_atlas.GetGlyph(ch, pen, 0.0f, q);
    }
    return pen * scale;
}

void UiRenderer::BuildText(std::vector<UiVertex>& out, const UI::DrawCommand& command,
                           float w, float h)
{
    const float scale = command.fontScale > 0.0f ? command.fontScale : 1.0f;
    const float textWidth = MeasureText(command.text, scale);
    const float ascent = m_atlas.Ascent() * scale;
    const float descent = m_atlas.Descent() * scale;
    const UI::Rect& r = command.rect;

    float originX = r.x;
    if (command.hAlign == UI::Align::Center) {
        originX = r.x + (r.width - textWidth) * 0.5f;
    } else if (command.hAlign == UI::Align::End) {
        originX = r.x + r.width - textWidth;
    }
    float baseline = r.y + ascent; // Start / top
    if (command.vAlign == UI::Align::Center) {
        baseline = r.y + (r.height - (ascent + descent)) * 0.5f + ascent;
    } else if (command.vAlign == UI::Align::End) {
        baseline = r.y + r.height - descent;
    }

    const std::uint32_t abgr = ToAbgr(command.color);
    float nativePen = 0.0f;
    std::size_t i = 0;
    while (i < command.text.size()) {
        const char ch = NextAsciiGlyph(command.text, i);
        if (ch == '\0') { break; }
        FontGlyphQuad q;
        // Lay the glyph out at a native baseline of 0, then scale about the line
        // origin so fontScale grows text without re-baking the atlas.
        if (m_atlas.GetGlyph(ch, nativePen, 0.0f, q)) {
            PushQuad(out,
                     originX + q.x0 * scale, baseline + q.y0 * scale,
                     originX + q.x1 * scale, baseline + q.y1 * scale,
                     q.s0, q.t0, q.s1, q.t1, abgr, w, h);
        }
    }
}

void UiRenderer::SubmitBatch(RenderViewHandle view, std::uint32_t width, std::uint32_t height,
                             const std::vector<UiVertex>& verts, bgfx::TextureHandle texture,
                             bgfx::ProgramHandle program)
{
    if (verts.empty()) {
        return;
    }
    const std::uint32_t count = static_cast<std::uint32_t>(verts.size());
    if (bgfx::getAvailTransientVertexBuffer(count, m_layout) < count) {
        return;
    }
    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, count, m_layout);
    std::memcpy(tvb.data, verts.data(), sizeof(UiVertex) * count);

    bgfx::setViewRect(view, 0, 0, static_cast<std::uint16_t>(width),
                      static_cast<std::uint16_t>(height));
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A
                   | BGFX_STATE_BLEND_ALPHA | BGFX_STATE_DEPTH_TEST_ALWAYS);
    bgfx::setTexture(0, m_sTex, texture);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::submit(view, program);
}

void UiRenderer::Draw(RenderViewHandle view, std::uint32_t width, std::uint32_t height,
                      const UI::DrawList& drawList, BgfxTextureCache* textures)
{
    if (!m_ready || drawList.Empty() || width == 0 || height == 0
        || view == kInvalidRenderView || !bgfx::isValid(m_program)) {
        return;
    }
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);

    std::vector<UiVertex> solids;
    std::vector<UiVertex> text;
    std::vector<ImageBatch> images;
    solids.reserve(drawList.commands.size() * 6u);
    for (const UI::DrawCommand& command : drawList.commands) {
        const UI::Rect& r = command.rect;
        if (command.kind == UI::DrawKind::Text) {
            BuildText(text, command, w, h);
            continue;
        }
        if (command.kind == UI::DrawKind::StyledRect) {
            PushRoundedRect(solids, command, w, h);
            continue;
        }
        if (command.kind == UI::DrawKind::TexturedRect && command.texture != 0
            && textures != nullptr) {
            // Resolve here (render thread): a path that failed to decode drops
            // to the solid tint below rather than sampling the single-channel
            // white fallback through the RGBA program, which would read red.
            const bgfx::TextureHandle handle =
                textures->Get(static_cast<TextureId>(command.texture));
            if (bgfx::isValid(handle)) {
                // One batch per distinct texture, in first-use order, so image
                // layering follows submission order like every other command.
                auto batch = std::find_if(images.begin(), images.end(),
                                          [&command](const ImageBatch& candidate) {
                                              return candidate.texture == command.texture;
                                          });
                if (batch == images.end()) {
                    images.push_back(ImageBatch{command.texture, handle, {}});
                    batch = std::prev(images.end());
                }
                PushQuad(batch->verts, r.x, r.y, r.x + r.width, r.y + r.height,
                         0.0f, 0.0f, 1.0f, 1.0f, ToAbgr(command.color), w, h);
                continue;
            }
        }
        // SolidRect, and any textured rect whose texture cannot be resolved:
        // drawing the tint keeps the layout intact instead of leaving a hole.
        PushQuad(solids, r.x, r.y, r.x + r.width, r.y + r.height,
                 0.0f, 0.0f, 1.0f, 1.0f, ToAbgr(command.color), w, h);
    }

    SubmitBatch(view, width, height, solids, m_white, m_program);
    for (const ImageBatch& batch : images) {
        SubmitBatch(view, width, height, batch.verts, batch.handle, m_imageProgram);
    }
    SubmitBatch(view, width, height, text, m_atlas.Texture(), m_program);
}

} // namespace Concord
