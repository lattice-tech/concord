#ifndef CONCORD_SKELETALEVENTSAMPLER_H
#define CONCORD_SKELETALEVENTSAMPLER_H

#include "Concord/CExport.h"
#include "engine/animation/clip/PlaybackMode.h"
#include "engine/animation/clip/SkeletalEventTrack.h"

#include <functional>
#include <vector>

namespace Concord::Animation {

/** One event delivered to a callback: where on the timeline it fired. */
struct SkeletalEvent {
    float time = 0.0f;
    std::string name;
    std::string payload;
};

/**
 * @brief Fires the markers of a SkeletalEventTrack as playback crosses them.
 *
 * Tracks the last sampled time and, on each Collect, delivers every event
 * whose time lies in the just-traversed window — exactly the ones a human
 * watching the clip would see pass. One sampler per playing instance, so
 * many characters sharing one clip each get their own callback stream.
 *
 * Firing semantics are deterministic:
 *  - The window is open at the *departure* time and closed at the arrival:
 *    forward, events with `from < t <= to` fire; backward, `to <= t < from`.
 *    A marker is therefore never delivered twice for a zero-progress frame.
 *  - On the first Collect the window starts below zero, so a marker at t = 0
 *    fires with the first frame.
 *  - Looping playback that wraps around the duration splits into two windows
 *    (the tail and the head); markers at the wrap point fire exactly once.
 *  - SetTime (a seek) rewinds the clock without firing anything — the caller
 *    decides what "skipping" means, the sampler never floods callbacks.
 *
 * Known limitation: a single step that advances more than one full loop
 * (e.g. a 3s frame on a 1s clip) reports the markers of the first and last
 * partial windows only; the intermediate whole turns are not replayed.
 * Normal frames advance far less than one clip length, so this only matters
 * under extreme hitches.
 *
 * Callbacks are instance-level (plain std::function list), deliberately not
 * the global Event Bus: animation events fire per-character per-frame and
 * would otherwise flood a process-wide queue.
 */
class CENGINE_API SkeletalEventSampler {
public:
    using Callback = std::function<void(const SkeletalEvent&)>;

    /** Registers a callback; an empty one is ignored. */
    void AddCallback(Callback callback);

    /** Removes every registered callback. */
    void ClearCallbacks() noexcept { m_callbacks.clear(); }

    /**
     * @brief Advances to @p toTime and fires the markers crossed.
     *
     * @param track    The event markers (may be empty; then this is a no-op).
     * @param toTime   New playback time; must stay within [0, duration] for
     *                 wrapped loops. Lower than the previous time means
     *                 backward playback.
     * @param duration Clip length; drives the wrap split.
     * @param mode     Only Loop wraps around the duration; other modes clamp.
     * @param forward  Playback direction (speed >= 0). Needed because a
     *                 backward wrap (0.1 -> 0.9) is indistinguishable from a
     *                 forward non-wrap by times alone.
     */
    void Collect(const SkeletalEventTrack& track, float toTime, float duration,
                 PlaybackMode mode, bool forward);

    /** Jumps the clock to @p time without firing anything (a seek). */
    void SetTime(float time) noexcept
    {
        m_lastTime = time;
        m_initialized = true;
    }

    /** Resets to the unplayed state (first Collect fires markers at t = 0). */
    void Reset() noexcept
    {
        m_lastTime = 0.0f;
        m_initialized = false;
    }

    /** The most recently reached playback time. */
    float LastTime() const noexcept { return m_lastTime; }

private:
    float m_lastTime = 0.0f;
    bool m_initialized = false;
    std::vector<Callback> m_callbacks;
};

} // namespace Concord::Animation

#endif // CONCORD_SKELETALEVENTSAMPLER_H
