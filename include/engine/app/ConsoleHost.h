#ifndef CONCORD_CONSOLEHOST_H
#define CONCORD_CONSOLEHOST_H

#include "Concord/CExport.h"
#include "engine/app/RuntimeMode.h"

namespace Concord {

/**
 * One-time, process-wide console policy for the engine's runtime mode.
 *
 * Executables built against the engine are console-subsystem binaries, so a
 * double-clicked run opens a console window next to the game window. This
 * policy hides it on Release runs (the game window is the only UI) and
 * guarantees Debug runs a visible console for the logger: a terminal that
 * launched the process is kept, and a double-clicked process gets one
 * allocated (AllocConsole) with stdout/stderr/stdin redirected to it.
 *
 * Windows-only; a no-op on other platforms. Call before any subsystem logs;
 * the Game constructor does this right after loading its config.
 */
CENGINE_API void ApplyConsolePolicy(RuntimeMode mode) noexcept;

} // namespace Concord

#endif // CONCORD_CONSOLEHOST_H
