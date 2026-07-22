#ifndef CONCORD_DEBUGFONTATLAS_H
#define CONCORD_DEBUGFONTATLAS_H

#include <bgfx/bgfx.h>

#include <cstdint>

namespace Concord {

/**
 * A single positioned glyph: a pixel-space rectangle (top-left origin, y down)
 * plus the atlas texture coordinates that cover it.
 */
struct FontGlyphQuad {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float s0 = 0.0f;
    float t0 = 0.0f;
    float s1 = 0.0f;
    float t1 = 0.0f;
};

/**
 * Bakes the embedded TrueType overlay font (Roboto) into a single-channel
 * (R8) glyph atlas once, and hands out per-glyph quads for layout.
 *
 * Replaces the previous 8x8 bitmap font: that font was scaled 3x as solid
 * pixel blocks, so every glyph read as a mosaic. A real anti-aliased TTF atlas
 * renders one textured quad per glyph — sharper on screen and two orders of
 * magnitude fewer vertices.
 */
class DebugFontAtlas {
public:
    bool EnsureReady();
    void Shutdown();

    bool Ready() const { return m_ready; }
    bgfx::TextureHandle Texture() const { return m_texture; }

    /** Distance to advance the baseline between lines, in pixels. */
    float LineHeight() const { return m_lineHeight; }
    /** Baseline-to-top distance (positive), in pixels. */
    float Ascent() const { return m_ascent; }
    /** Baseline-to-bottom distance (positive), in pixels. */
    float Descent() const { return m_descent; }

    /**
     * Fills @p out for ASCII glyph @p c relative to the pen at (@p penX,
     * @p penY = baseline) and advances @p penX. Returns false for glyphs
     * outside the baked range (caller should substitute a fallback).
     */
    bool GetGlyph(char c, float& penX, float penY, FontGlyphQuad& out) const;

private:
    /** Mirrors stbtt_bakedchar's layout so stb types stay out of this header. */
    struct BakedGlyph {
        std::uint16_t x0 = 0;
        std::uint16_t y0 = 0;
        std::uint16_t x1 = 0;
        std::uint16_t y1 = 0;
        float xoff = 0.0f;
        float yoff = 0.0f;
        float xadvance = 0.0f;
    };

    static constexpr int kFirstGlyph = 32;
    static constexpr int kGlyphCount = 95; // 32..126

    bool m_ready = false;
    bgfx::TextureHandle m_texture = BGFX_INVALID_HANDLE;
    float m_atlasWidth = 0.0f;
    float m_atlasHeight = 0.0f;
    float m_lineHeight = 0.0f;
    float m_ascent = 0.0f;
    float m_descent = 0.0f;
    BakedGlyph m_glyphs[kGlyphCount] = {};
};

} // namespace Concord

#endif // CONCORD_DEBUGFONTATLAS_H
