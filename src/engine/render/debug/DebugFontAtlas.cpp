#include "engine/render/debug/DebugFontAtlas.h"

#include "engine/debug/Logger.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb/stb_truetype.h>

#include <font/OverlayFontData.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace Concord {

namespace {

// Display pixel height glyphs are baked at. Roboto stays crisp for a debug
// overlay around this size; the atlas is generously sized so all 95 printable
// ASCII glyphs pack in one row set.
constexpr int kPixelHeight = 24;
constexpr int kAtlasWidth = 512;
constexpr int kAtlasHeight = 256;

} // namespace

bool DebugFontAtlas::EnsureReady()
{
    if (m_ready) {
        return true;
    }

    std::vector<unsigned char> bitmap(static_cast<std::size_t>(kAtlasWidth) * kAtlasHeight, 0);
    stbtt_bakedchar baked[kGlyphCount] = {};
    const int result = stbtt_BakeFontBitmap(kOverlayFontTtf, 0,
                                            static_cast<float>(kPixelHeight),
                                            bitmap.data(), kAtlasWidth, kAtlasHeight,
                                            kFirstGlyph, kGlyphCount, baked);
    if (result == 0) {
        Debug::Logger::Error("Render", "overlay font bake failed (atlas too small)");
        return false;
    }

    for (int i = 0; i < kGlyphCount; ++i) {
        m_glyphs[i].x0 = baked[i].x0;
        m_glyphs[i].y0 = baked[i].y0;
        m_glyphs[i].x1 = baked[i].x1;
        m_glyphs[i].y1 = baked[i].y1;
        m_glyphs[i].xoff = baked[i].xoff;
        m_glyphs[i].yoff = baked[i].yoff;
        m_glyphs[i].xadvance = baked[i].xadvance;
    }

    stbtt_fontinfo info;
    if (stbtt_InitFont(&info, kOverlayFontTtf,
                       stbtt_GetFontOffsetForIndex(kOverlayFontTtf, 0)) != 0) {
        const float scale = stbtt_ScaleForPixelHeight(&info, static_cast<float>(kPixelHeight));
        int ascent = 0;
        int descent = 0;
        int lineGap = 0;
        stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
        m_ascent = static_cast<float>(ascent) * scale;
        m_descent = static_cast<float>(-descent) * scale;
        m_lineHeight = static_cast<float>(ascent - descent + lineGap) * scale;
    } else {
        m_ascent = static_cast<float>(kPixelHeight) * 0.8f;
        m_descent = static_cast<float>(kPixelHeight) * 0.2f;
        m_lineHeight = static_cast<float>(kPixelHeight) * 1.2f;
    }

    const bgfx::Memory* mem = bgfx::copy(bitmap.data(),
                                         static_cast<std::uint32_t>(bitmap.size()));
    m_texture = bgfx::createTexture2D(
        kAtlasWidth, kAtlasHeight, false, 1, bgfx::TextureFormat::R8,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP, mem);
    if (!bgfx::isValid(m_texture)) {
        Debug::Logger::Error("Render", "overlay font atlas texture creation failed");
        return false;
    }

    m_atlasWidth = static_cast<float>(kAtlasWidth);
    m_atlasHeight = static_cast<float>(kAtlasHeight);
    m_ready = true;
    return true;
}

void DebugFontAtlas::Shutdown()
{
    if (bgfx::isValid(m_texture)) {
        bgfx::destroy(m_texture);
        m_texture = BGFX_INVALID_HANDLE;
    }
    m_ready = false;
}

bool DebugFontAtlas::GetGlyph(char c, float& penX, float penY, FontGlyphQuad& out) const
{
    const int code = static_cast<unsigned char>(c);
    if (code < kFirstGlyph || code >= kFirstGlyph + kGlyphCount) {
        return false;
    }
    const BakedGlyph& b = m_glyphs[code - kFirstGlyph];

    // Same placement math as stbtt_GetBakedQuad (opengl_fillrule = 1, no bias).
    const float roundX = std::floor((penX + b.xoff) + 0.5f);
    const float roundY = std::floor((penY + b.yoff) + 0.5f);
    out.x0 = roundX;
    out.y0 = roundY;
    out.x1 = roundX + static_cast<float>(b.x1 - b.x0);
    out.y1 = roundY + static_cast<float>(b.y1 - b.y0);
    out.s0 = static_cast<float>(b.x0) / m_atlasWidth;
    out.t0 = static_cast<float>(b.y0) / m_atlasHeight;
    out.s1 = static_cast<float>(b.x1) / m_atlasWidth;
    out.t1 = static_cast<float>(b.y1) / m_atlasHeight;

    penX += b.xadvance;
    return true;
}

} // namespace Concord
