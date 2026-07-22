#ifndef CONCORD_TIME_H
#define CONCORD_TIME_H

#include "Concord/CExport.h"

#include <cstdint>

namespace Concord {

/**
 * Blocks the calling (logic) thread for `milliseconds`.
 *
 * This only pauses the thread that calls it — typically the thread running
 * game logic. The engine's render loop lives on its own thread (see the
 * engine main loop), so an attached window keeps drawing and stays
 * responsive for the whole duration of the sleep.
 *
 * @param milliseconds How long to pause the calling thread, in milliseconds.
 */
CTIME_API void Sleep(std::uint32_t milliseconds);

/**
 * Milliseconds elapsed since the time module was first used.
 *
 * Backed by a monotonic clock, so it never jumps or runs backward when the
 * wall clock is adjusted — suitable for measuring durations, not for
 * calendar time. The zero point is the process's first call into the time
 * module, which is an implementation detail: only differences are meaningful.
 */
CTIME_API std::uint64_t Ticks();

/** Seconds elapsed since the time module was first used; same clock as Ticks. */
CTIME_API double Seconds();

} // namespace Concord

#endif // CONCORD_TIME_H
