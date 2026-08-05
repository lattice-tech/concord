#include "engine/animation/clip/SyncTrack.h"

#include <cmath>
#include <utility>

namespace Concord::Animation {
namespace {

/** Indices of every marker named @p name, in track order. */
std::vector<std::size_t> NamedIndices(const SyncTrack& track,
                                      const std::string& name)
{
    std::vector<std::size_t> indices;
    const auto& points = track.Points();
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (points[i].name == name) {
            indices.push_back(i);
        }
    }
    return indices;
}

} // namespace

bool SyncTrack::AddSync(float time, std::string name)
{
    if (!std::isfinite(time) || time < 0.0f) {
        return false;
    }
    SyncPoint point;
    point.time = time;
    point.name = std::move(name);

    auto it = m_points.begin();
    while (it != m_points.end() && it->time <= time) {
        ++it;
    }
    m_points.insert(it, point);
    return true;
}

float SyncTrack::MapTime(float sourceTime, const SyncTrack& source,
                         const SyncTrack& target, const std::string& syncName,
                         float sourceDuration, float targetDuration)
{
    if (sourceDuration <= 0.0f || targetDuration <= 0.0f) {
        return sourceTime;
    }
    const std::vector<std::size_t> sourceMarkers = NamedIndices(source, syncName);
    const std::vector<std::size_t> targetMarkers = NamedIndices(target, syncName);
    if (sourceMarkers.empty() || targetMarkers.empty()) {
        // No shared markers: fall back to the normalised phase.
        const float clamped = sourceTime < 0.0f ? 0.0f
            : (sourceTime > sourceDuration ? sourceDuration : sourceTime);
        return clamped / sourceDuration * targetDuration;
    }

    const auto& sourcePoints = source.Points();
    const auto& targetPoints = target.Points();

    // Current interval on the source: the last marker at or before the time.
    std::size_t interval = sourceMarkers.size();
    for (std::size_t i = 0; i < sourceMarkers.size(); ++i) {
        if (sourcePoints[sourceMarkers[i]].time > sourceTime) {
            interval = i;
            break;
        }
    }
    if (interval == 0) {
        // Before the first marker: phase fallback.
        return sourceTime / sourceDuration * targetDuration;
    }
    const std::size_t startIndex = interval - 1;

    const float startSource = sourcePoints[sourceMarkers[startIndex]].time;
    const float startTarget = targetPoints[targetMarkers[startIndex]].time;

    // Inside [m0, m1]: preserve the fractional position within the interval.
    if (interval < sourceMarkers.size()) {
        const float endSource = sourcePoints[sourceMarkers[interval]].time;
        const float span = endSource - startSource;
        const float u = span > 1.0e-6f
            ? (sourceTime - startSource) / span
            : 0.0f;
        if (interval < targetMarkers.size()) {
            const float endTarget = targetPoints[targetMarkers[interval]].time;
            return startTarget + u * (endTarget - startTarget);
        }
        // Target ran out of markers: hold at the target's last marker.
        return startTarget;
    }

    // After the last source marker: hold at the last aligned position, which
    // keeps the tail of the loop pinned to the shared marker.
    return startTarget;
}

} // namespace Concord::Animation
