#ifndef CONCORD_BLENDSPACE2D_H
#define CONCORD_BLENDSPACE2D_H

#include "Concord/CExport.h"
#include "engine/animation/blend/Pose.h"
#include "math/Vector2.h"

#include <cstdint>
#include <vector>

namespace Concord::Animation {

class AnimationClip;

namespace Detail {
struct BlendTriangle;
} // namespace Detail

/**
 * @brief A two-dimensional blend space over animation clips.
 *
 * Clips are placed at positions on a plane (two parameters, e.g. a
 * speed x turn-rate axis or a gamepad-stick x/y), the sample positions are
 * triangulated, and the pose at any control point is the barycentric blend
 * of the three clips around it. Outside the convex hull the query clamps to
 * the nearest edge (a two-clip blend), so the output stays continuous.
 *
 * Like BlendSpace1D the clips are phase-synchronised: every clip is sampled
 * at the same normalised phase [0, 1), so footfalls line up across clips of
 * different lengths. Degenerate layouts degrade deterministically: one clip
 * always wins, and collinear samples fall back to a 1D blend along their
 * longest axis. Clips are referenced, never owned, and must outlive the
 * blend space.
 */
class CENGINE_API BlendSpace2D {
public:
    /** Adds a clip at blend-space position @p position (order-independent). */
    void AddClip(Vector2 position, const AnimationClip* clip);

    bool Empty() const noexcept { return m_entries.empty(); }

    /** Number of placed clips. */
    std::size_t Count() const noexcept { return m_entries.size(); }

    /**
     * The longest referenced clip's duration — the blend space's own
     * duration, so a driver can advance a shared phase clock over it.
     */
    float Duration() const noexcept;

    /**
     * Pose at control position (@p x, @p y) and normalised phase @p phase in
     * [0, 1). Each contributing clip is sampled at `phase * clipDuration`.
     */
    Pose Sample(float x, float y, float phase) const;

private:
    struct Entry {
        Vector2 position{};
        const AnimationClip* clip = nullptr;
    };

    /** Rebuilds the cached triangulation when the sample set changed. */
    void RebuildTriangulation() const;

    static Pose SampleEntry(const Entry& entry, float phase);

    std::vector<Entry> m_entries;

    mutable std::vector<Vector2> m_points;
    mutable std::vector<Detail::BlendTriangle> m_triangles;
    mutable std::vector<std::uint32_t> m_remap;
    mutable bool m_triangulationValid = false;
};

} // namespace Concord::Animation

#endif // CONCORD_BLENDSPACE2D_H
