#ifndef CONCORD_SKELETALEVENTTRACK_H
#define CONCORD_SKELETALEVENTTRACK_H

#include "Concord/CExport.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Concord::Animation {

/**
 * One named marker on an animation timeline. Fired when a sampler crosses its
 * time while advancing — game code hooks footstep sounds, impact frames and
 * state changes to the clip's playback instead of guessing from time.
 */
struct SkeletalEventPoint {
    /** Time in seconds on the clip timeline. */
    float time = 0.0f;

    /** Machine-readable event name, e.g. "footstep" or "hit". */
    std::string name;

    /** Optional free-form payload (e.g. a foot name or an impact sound path). */
    std::string payload;
};

/**
 * @brief An ordered list of named time markers for one clip.
 *
 * Pure authoring data with no playback state: points stay sorted by time
 * (stable at equal times, insertion order preserved) so a sampler can walk a
 * window in one pass. Multiple clips or characters may share one track.
 * Negative or non-finite times are rejected.
 *
 * A track is typically embedded in a clip (SkeletalClip::events), but it is a
 * plain component and can be driven standalone over any timeline.
 */
class CENGINE_API SkeletalEventTrack {
public:
    /**
     * @brief Inserts a marker, keeping the list time-sorted.
     * @return false when @p time is negative or non-finite; nothing is added.
     */
    bool AddEvent(float time, std::string name, std::string payload = {});

    bool Empty() const noexcept { return m_points.empty(); }

    std::size_t Count() const noexcept { return m_points.size(); }

    /** The time-sorted points (stable at equal times). */
    const std::vector<SkeletalEventPoint>& Points() const noexcept
    {
        return m_points;
    }

private:
    std::vector<SkeletalEventPoint> m_points;
};

} // namespace Concord::Animation

#endif // CONCORD_SKELETALEVENTTRACK_H
