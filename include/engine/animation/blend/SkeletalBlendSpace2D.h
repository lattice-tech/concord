#ifndef CONCORD_SKELETALBLENDSPACE2D_H
#define CONCORD_SKELETALBLENDSPACE2D_H

#include "Concord/CExport.h"
#include "engine/animation/skeleton/Skeleton.h"
#include "math/Vector2.h"

#include <cstdint>
#include <vector>

namespace Concord::Animation {

class SkeletalClip;
class Skeleton;

namespace Detail {
struct BlendTriangle;
} // namespace Detail

/**
 * @brief A two-dimensional blend space over skeletal clips.
 *
 * The skinned-character analogue of BlendSpace2D: clips are placed on a
 * plane, the sample positions are triangulated, and the whole-skeleton pose
 * at any control point is the bone-by-bone barycentric blend of the three
 * surrounding clips. Same phase-synchronisation and degenerate fallbacks as
 * BlendSpace2D (see there). Clips are referenced, never owned.
 */
class CENGINE_API SkeletalBlendSpace2D {
public:
    /** Adds a clip at blend-space position @p position (order-independent). */
    void AddClip(Vector2 position, const SkeletalClip* clip);

    bool Empty() const noexcept { return m_entries.empty(); }

    /** Number of placed clips. */
    std::size_t Count() const noexcept { return m_entries.size(); }

    /**
     * The longest referenced clip's duration — the blend space's own
     * duration, so a driver can advance a shared phase clock over it.
     */
    float Duration() const noexcept;

    /**
     * Samples the pose at control position (@p x, @p y) and normalised phase
     * @p phase in [0, 1) over @p skeleton into @p out. Each contributing clip
     * is sampled at `phase * clipDuration`.
     */
    void Sample(float x, float y, float phase, const Skeleton& skeleton,
                SkeletonPose& out) const;

private:
    struct Entry {
        Vector2 position{};
        const SkeletalClip* clip = nullptr;
    };

    /** Rebuilds the cached triangulation when the sample set changed. */
    void RebuildTriangulation() const;

    static void SampleEntry(const Entry& entry, float phase,
                            const Skeleton& skeleton, SkeletonPose& out);

    std::vector<Entry> m_entries;

    mutable std::vector<Vector2> m_points;
    mutable std::vector<Detail::BlendTriangle> m_triangles;
    mutable std::vector<std::uint32_t> m_remap;
    mutable bool m_triangulationValid = false;
};

} // namespace Concord::Animation

#endif // CONCORD_SKELETALBLENDSPACE2D_H
