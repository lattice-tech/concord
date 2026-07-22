#ifndef CONCORD_UISURFACE_H
#define CONCORD_UISURFACE_H

#include "Concord/CExport.h"
#include "engine/ui/UiDrawList.h"

namespace Concord::UI {

/**
 * Thread-safe hand-off of the current UI draw list from the simulation thread
 * (where the game builds it via Context) to the render thread (which draws it).
 *
 * Mirrors the proven Debug overlay bridge: the game submits the latest draw list
 * each frame; the render thread snapshots it while drawing the window overlay.
 * The most recent submission wins, matching the engine's "render replays the
 * latest published snapshot" model, so a slow frame never blocks drawing.
 */

/** Replaces the draw list the render thread will draw next. Safe from any thread. */
CENGINE_API void Submit(const DrawList& drawList);

/** Clears the UI so nothing is drawn until the next Submit. */
CENGINE_API void ClearSurface();

namespace Detail {

/** Render-thread only: copies the current UI draw list for drawing. */
void SnapshotDrawList(DrawList& out);

} // namespace Detail

} // namespace Concord::UI

#endif // CONCORD_UISURFACE_H
