#ifndef CONCORD_DEBUGTEXTOVERLAY_H
#define CONCORD_DEBUGTEXTOVERLAY_H

#include "engine/render/backend/IRenderBackend.h"
#include "engine/render/debug/DebugFontAtlas.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Concord {

/**
 * One line of on-screen debug text (bottom-left stack).
 */
struct DebugTextLine {
    std::string text;
    /** Packed 0xRRGGBBAA (same packing as materials / PrintString). */
    std::uint32_t color = 0xffffffffu;
};

/**
 * Draws temporary debug strings onto a window framebuffer.
 *
 * bgfx::dbgText always targets the process-wide primary backbuffer. Concord
 * initialises that as a hidden 8x8 device window and presents Game windows as
 * secondary swap-chains, so dbgText is invisible. This overlay submits
 * screen-space glyph quads into the view already bound to the visible window.
 */
class DebugTextOverlay {
public:
    bool EnsureReady();
    void Shutdown();

    /** Where a text block is anchored within the view. */
    enum class Anchor {
        BottomLeft, ///< Stacks upward from the bottom-left (newest at bottom).
        TopRight,   ///< Right-aligned, stacks downward from the top-right.
    };

    /**
     * Draws `lines` into the view bound to `view` (must already target the
     * window framebuffer). BottomLeft stacks upward (newest at the bottom, the
     * PrintString convention); TopRight right-aligns and stacks downward (for a
     * persistent HUD). Safe no-op when empty or not ready.
     */
    void Draw(RenderViewHandle view, std::uint32_t width, std::uint32_t height,
              const std::vector<DebugTextLine>& lines, Anchor anchor = Anchor::BottomLeft);

private:
    /** Pixel width of a line at the atlas's native size (for right alignment). */
    float MeasureLineWidth(const std::string& text);

    bool m_ready = false;
    bgfx::ProgramHandle m_program = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_sFont = BGFX_INVALID_HANDLE;
    bgfx::VertexLayout m_layout;
    DebugFontAtlas m_atlas;
};

} // namespace Concord

#endif // CONCORD_DEBUGTEXTOVERLAY_H
