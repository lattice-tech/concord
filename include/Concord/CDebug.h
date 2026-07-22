#ifndef CONCORD_CDEBUG_H
#define CONCORD_CDEBUG_H

/**
 * Public entry point for the diagnostic/logging system.
 *
 * Re-exports the real declarations under `engine/debug/` (facade pattern,
 * AGENTS.md §3). Application code that wants to log through the same channel
 * as the engine includes this and calls Concord::Debug::Logger; the log
 * verbosity follows the config file's `mode` (Debug vs Release).
 */

#include "engine/debug/DebugOverlay.h"
#include "engine/debug/LogLevel.h"
#include "engine/debug/Logger.h"

#endif // CONCORD_CDEBUG_H
