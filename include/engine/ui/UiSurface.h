#ifndef CONCORD_UISURFACE_H
#define CONCORD_UISURFACE_H

#include "Concord/CExport.h"
#include "engine/ui/UiDrawList.h"
#include "engine/window/WindowId.h"

namespace Concord::UI {

/**
 * Thread-safe hand-off of per-window UI draw lists from the simulation thread
 * (where each game builds one via Context) to the render thread.
 *
 * The most recent submission for each WindowId wins, matching the engine's
 * "render replays the latest published snapshot" model without leaking one
 * game's HUD into another window.
 */

/** Replaces the draw list for one window. Safe from any thread. */
CENGINE_API void Submit(WindowId window, const DrawList& drawList);

/**
 * Compatibility fallback for callers that do not identify a window.
 * Explicit per-window submissions take precedence over this process-wide list.
 */
CENGINE_API void Submit(const DrawList& drawList);

/** Clears the explicit UI submission for one window. */
CENGINE_API void ClearSurface(WindowId window);

/** Clears only the process-wide compatibility fallback. */
CENGINE_API void ClearSurface();

namespace Detail {

/** Render-thread only: snapshots a window list, falling back when none exists. */
CENGINE_API void SnapshotDrawList(WindowId window, DrawList& out);

/** Render-thread compatibility hook: snapshots only the fallback draw list. */
CENGINE_API void SnapshotDrawList(DrawList& out);

} // namespace Detail

} // namespace Concord::UI

#endif // CONCORD_UISURFACE_H
