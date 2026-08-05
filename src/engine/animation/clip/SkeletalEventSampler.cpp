#include "engine/animation/clip/SkeletalEventSampler.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Concord::Animation {
namespace {

/**
 * One marker that lies inside the traversed window, tagged with the window
 * segment it belongs to (0 fires before 1 during a wrap).
 */
struct FiredMarker {
    int segment = 0;
    float time = 0.0f;
    const SkeletalEventPoint* point = nullptr;
};

/**
 * Appends every marker crossed in this window segment to @p out. The window
 * is open at the *departure* end and closed at the *arrival* end, in both
 * directions: forward delivers `departure < t <= arrival`, backward delivers
 * `arrival <= t < departure`. A marker is therefore never delivered twice for
 * a zero-progress frame. The track is time-sorted, so the collected markers
 * come out ascending; a backward segment reverses them to the order they are
 * crossed.
 */
void CollectSegment(const SkeletalEventTrack& track, float departure,
                    float arrival, bool forward, int segment,
                    std::vector<FiredMarker>& out)
{
    const std::size_t begin = out.size();
    for (const SkeletalEventPoint& point : track.Points()) {
        if (forward) {
            if (point.time <= departure || point.time > arrival) {
                continue;
            }
        } else {
            if (point.time < arrival || point.time >= departure) {
                continue;
            }
        }
        out.push_back(FiredMarker{segment, point.time, &point});
    }
    if (!forward) {
        std::reverse(out.begin() + static_cast<std::ptrdiff_t>(begin), out.end());
    }
}

} // namespace

void SkeletalEventSampler::AddCallback(Callback callback)
{
    if (callback) {
        m_callbacks.push_back(std::move(callback));
    }
}

void SkeletalEventSampler::Collect(const SkeletalEventTrack& track,
                                   float toTime, float duration,
                                   PlaybackMode mode, bool forward)
{
    if (track.Empty() || m_callbacks.empty()) {
        m_lastTime = toTime;
        m_initialized = true;
        return;
    }

    // The first frame starts below zero so markers at t = 0 fire immediately.
    const float from = m_initialized ? m_lastTime : -1.0f;
    m_lastTime = toTime;
    m_initialized = true;

    if (!std::isfinite(from) || !std::isfinite(toTime)) {
        return;
    }
    if (duration <= 0.0f) {
        return;
    }

    const bool wrapped = mode == PlaybackMode::Loop
        && (forward ? from > toTime : from < toTime);

    std::vector<FiredMarker> fired;
    if (!wrapped) {
        // A single window from the departure time to the arrival time. The
        // open/closed boundary rule is expressed by CollectSegment.
        CollectSegment(track, from, toTime, forward, 0, fired);
    } else if (forward) {
        // Wrapped forward: tail (from -> duration), then head (0 -> to).
        CollectSegment(track, from, duration, true, 0, fired);
        CollectSegment(track, -1.0f, toTime, true, 1, fired);
    } else {
        // Wrapped backward: head backwards (from -> 0), then tail backwards
        // (duration -> to). The arrival end of each segment is closed.
        CollectSegment(track, from, 0.0f, false, 0, fired);
        CollectSegment(track, duration, toTime, false, 1, fired);
    }

    if (fired.empty()) {
        return;
    }
    // Each segment's markers were already emitted in crossing order (ascending
    // forward, descending backward); ordering by segment alone merges the two
    // wrap windows without disturbing that. A stable sort keeps equal times in
    // their insertion order.
    std::stable_sort(fired.begin(), fired.end(),
                     [](const FiredMarker& a, const FiredMarker& b) {
                         return a.segment < b.segment;
                     });

    for (const FiredMarker& marker : fired) {
        SkeletalEvent event;
        event.time = marker.time;
        event.name = marker.point->name;
        event.payload = marker.point->payload;
        for (const Callback& callback : m_callbacks) {
            callback(event);
        }
    }
}

} // namespace Concord::Animation
