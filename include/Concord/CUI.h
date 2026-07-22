#ifndef CONCORD_CUI_H
#define CONCORD_CUI_H

/**
 * Public entry point for the immediate-mode UI system.
 *
 * Re-exports the real declarations under engine/ui/ (facade pattern,
 * AGENTS.md §3); this header itself only forwards. This is a new system,
 * independent of the legacy (empty) CGUI module. A game builds its HUD/menus
 * each frame with a Concord::UI::Context, then hands the resulting draw list to
 * the renderer. See docs/UI系统.md for the architecture and phased plan.
 */

#include "engine/ui/UiContext.h"
#include "engine/ui/UiDocument.h"
#include "engine/ui/UiDocumentIO.h"
#include "engine/ui/UiDrawList.h"
#include "engine/ui/UiInput.h"
#include "engine/ui/UiStyle.h"
#include "engine/ui/UiSurface.h"
#include "engine/ui/UiTypes.h"

#endif // CONCORD_CUI_H
