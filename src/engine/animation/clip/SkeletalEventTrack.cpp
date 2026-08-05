#include "engine/animation/clip/SkeletalEventTrack.h"

#include <cmath>
#include <utility>

namespace Concord::Animation {

bool SkeletalEventTrack::AddEvent(float time, std::string name,
                                  std::string payload)
{
    if (!std::isfinite(time) || time < 0.0f) {
        return false;
    }
    SkeletalEventPoint point;
    point.time = time;
    point.name = std::move(name);
    point.payload = std::move(payload);

    // Sorted insert; equal times keep insertion order (insert after the last
    // equal time).
    auto it = m_points.begin();
    while (it != m_points.end() && it->time <= time) {
        ++it;
    }
    m_points.insert(it, point);
    return true;
}

} // namespace Concord::Animation
