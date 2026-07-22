#ifndef CONCORD_PRINTSTRING_H
#define CONCORD_PRINTSTRING_H

#include "Concord/CExport.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Concord {

/**
 * Options for a temporary on-screen debug line (bottom-left stack).
 *
 * Game code configures colour and lifetime; the render thread draws active
 * lines each frame onto the Game window and drops them when `duration` elapses.
 */
struct PrintStringOptions {
    /** Seconds the line stays visible (clamped to a small positive range). */
    float durationSeconds = 3.0f;

    /**
     * Text colour as packed 0xRRGGBBAA (same packing as materials). Alpha is
     * forced opaque for the overlay.
     */
    std::uint32_t color = 0xffffffffu;
};

/**
 * Queues a temporary debug string for the bottom-left overlay.
 *
 * Multiple active lines stack **upward** (newest at the bottom of the stack,
 * older lines above). Thread-safe: may be called from any thread; drawing
 * happens on the render thread at the end of each window's present.
 *
 * @param text ASCII content (glyph set is a basic 8x8 font for 32..126).
 * @param options Lifetime and colour (see PrintStringOptions).
 */
CENGINE_API void PrintString(const char* text, const PrintStringOptions& options = {});

/** Overload for std::string. */
CENGINE_API void PrintString(const std::string& text, const PrintStringOptions& options = {});

/**
 * Convenience: default colour, custom duration.
 * @param durationSeconds How long the line remains on screen.
 */
CENGINE_API void PrintString(const char* text, float durationSeconds);

namespace Detail {

/** One live overlay line after expiry pruning (render-thread snapshot). */
struct PrintStringLine {
    std::string text;
    std::uint32_t color = 0xffffffffu;
};

/**
 * Render-thread only: drop expired lines and copy the live set for drawing.
 * Called once per Game window present so multi-window stays consistent.
 */
void SnapshotPrintStrings(std::vector<PrintStringLine>& out);

} // namespace Detail

} // namespace Concord

#endif // CONCORD_PRINTSTRING_H
