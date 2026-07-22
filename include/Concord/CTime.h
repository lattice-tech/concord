#ifndef CONCORD_CTIME_H
#define CONCORD_CTIME_H

/**
 * Public entry point for time utilities (CTime.dll).
 *
 * This header only re-exports the real declarations from the time module's
 * private headers (see AGENTS.md §3, facade re-export pattern); application
 * code includes this file to reach Concord::Sleep, never the headers under
 * `time/`.
 */

#include "time/FrameCounter.h"
#include "time/Time.h"

#endif // CONCORD_CTIME_H
