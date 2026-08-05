#ifndef CONCORD_SYNCTRACK_H
#define CONCORD_SYNCTRACK_H

#include "Concord/CExport.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Concord::Animation {

/**
 * One named marker on a clip timeline used to *align* blended clips, as
 * opposed to SkeletalEventPoint which fires game callbacks. Two clips that
 * share a marker name (e.g. "footLeft") line up at those markers, so a walk
 * clip and a run clip of different lengths keep their footfalls together
 * while blending.
 */
struct SyncPoint {
    /** Time in seconds on the clip timeline. */
    float time = 0.0f;

    /** Marker name shared across clips that must align. */
    std::string name;
};

/**
 * @brief An ordered list of named alignment markers for one clip.
 *
 * Pure authoring data, like SkeletalEventTrack, but consumed by blend spaces
 * and state-machine crossfades instead of fired as callbacks. Markers stay
 * sorted by time; negative or non-finite times are rejected.
 */
class CENGINE_API SyncTrack {
public:
    /**
     * @brief Inserts a marker, keeping the list time-sorted.
     * @return false when @p time is negative or non-finite; nothing is added.
     */
    bool AddSync(float time, std::string name);

    bool Empty() const noexcept { return m_points.empty(); }

    std::size_t Count() const noexcept { return m_points.size(); }

    /** The time-sorted markers (stable at equal times). */
    const std::vector<SyncPoint>& Points() const noexcept { return m_points; }

    /**
     * @brief Maps a time on @p source onto @p target through shared markers.
     *
     * The marker named @p syncName is looked up on both tracks. Within a
     * marker interval the position inside the interval is preserved: at
     * 40% through source's [m0, m1] the result is 40% through target's
     * [m0, m1]. Outside the first marker (or with missing/empty tracks) the
     * mapping falls back to the normalised phase, so the blend degrades
     * smoothly instead of snapping.
     *
     * @param sourceDuration / @p targetDuration Clip lengths, used by the
     *                        phase fallback.
     */
    static float MapTime(float sourceTime, const SyncTrack& source,
                         const SyncTrack& target, const std::string& syncName,
                         float sourceDuration, float targetDuration);

private:
    std::vector<SyncPoint> m_points;
};

} // namespace Concord::Animation

#endif // CONCORD_SYNCTRACK_H
