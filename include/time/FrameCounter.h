#ifndef CONCORD_FRAMECOUNTER_H
#define CONCORD_FRAMECOUNTER_H

#include "Concord/CExport.h"

#include <chrono>
#include <cstdint>

namespace Concord {

/**
 * Counts frames and derives a frame rate from the caller's own Tick() calls
 * (CTime.dll).
 *
 * A standalone, reusable helper: it does not read the engine's render loop,
 * it measures whatever loop calls Tick() once per iteration. Application code
 * that wants an FPS readout for a loop it drives itself constructs one and
 * ticks it; the engine's built-in frame rate is exposed separately through
 * Game (see docs/主循环.md). It is not synchronized: use one instance from a
 * single thread.
 */
class CTIME_API FrameCounter {
public:
    /**
     * Records one frame; call once per iteration of the loop being measured.
     * @return Seconds elapsed since the previous Tick (0 on the first call).
     */
    float Tick() noexcept;

    /** Total number of Tick() calls since construction or the last Reset(). */
    std::uint64_t FrameCount() const noexcept;

    /** Seconds between the two most recent Tick() calls. */
    float DeltaTime() const noexcept;

    /** Smoothed frames per second, from an exponential moving average of recent deltas. */
    double Fps() const noexcept;

    /** Clears the count, timing and rate back to their initial state. */
    void Reset() noexcept;

private:
    std::chrono::steady_clock::time_point m_lastTick = std::chrono::steady_clock::now();
    std::uint64_t m_frameCount = 0;
    float m_deltaTime = 0.0f;
    double m_fps = 0.0;
    bool m_started = false;
};

} // namespace Concord

#endif // CONCORD_FRAMECOUNTER_H
