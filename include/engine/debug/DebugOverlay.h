#ifndef CONCORD_DEBUGOVERLAY_H
#define CONCORD_DEBUGOVERLAY_H

#include "Concord/CExport.h"
#include "engine/utils/PrintString.h"

#include <string>
#include <vector>

namespace Concord::Debug {

/**
 * A persistent, replace-each-call on-screen debug text block.
 *
 * DEBUG / DEVELOPMENT ONLY — not intended for shipping game UI. Unlike
 * Concord::PrintString (temporary, timed, stacking lines), this holds a single
 * block that stays until replaced or cleared, so a caller can update it every
 * frame with live diagnostics (FPS, time-of-day, counters) without stacking.
 * Split lines with '\n'. Thread-safe; drawn by the render thread each frame.
 *
 * Prefer building a real, purpose-made HUD for anything that ships.
 */
CENGINE_API void SetDebugOverlay(const std::string& text);

/** Clears the persistent debug overlay. */
CENGINE_API void ClearDebugOverlay();

namespace Detail {

/** Render-thread only: copies the current persistent overlay lines for drawing. */
void SnapshotDebugOverlay(std::vector<Concord::Detail::PrintStringLine>& out);

} // namespace Detail

} // namespace Concord::Debug

#endif // CONCORD_DEBUGOVERLAY_H
