#ifndef CONCORD_FRAMECLOCK_H
#define CONCORD_FRAMECLOCK_H

#include <atomic>
#include <chrono>
#include <cstdint>

namespace Concord {

/**
 * Tracks the time elapsed between successive Tick() calls so it can be
 * read back, from any thread, as a plain float (seconds) via DeltaTime().
 *
 * Tick() is meant to be called once per iteration by whichever thread owns
 * the frame loop (see EngineLoop). DeltaTime() only ever reads back the
 * value the most recent Tick() stored, so it is safe to call from any
 * other thread at any time.
 */
class FrameClock {
public:
    /** Marks "now" as the end of this frame; returns the time (seconds) since the previous call. */
    float Tick() noexcept;

    /** Time elapsed between the two most recent Tick() calls, in seconds. */
    float DeltaTime() const noexcept;

    /** Total number of Tick() calls since construction; readable from any thread. */
    std::uint64_t FrameCount() const noexcept;

    /** Smoothed frames per second (exponential moving average); readable from any thread. */
    float Fps() const noexcept;

private:
    std::chrono::steady_clock::time_point m_lastTick = std::chrono::steady_clock::now();
    std::atomic<float> m_deltaTime{0.0f};
    std::atomic<std::uint64_t> m_frameCount{0};
    std::atomic<float> m_fps{0.0f};
};

} // namespace Concord

#endif // CONCORD_FRAMECLOCK_H
