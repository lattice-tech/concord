#ifndef CONCORD_SKELETONREMAPPER_H
#define CONCORD_SKELETONREMAPPER_H

#include "Concord/CExport.h"
#include "engine/animation/skeleton/Skeleton.h"

#include <vector>

namespace Concord::Animation {

class SkeletalClip;

/**
 * @brief Re-targets one skeletal clip onto a different skeleton by bone name.
 *
 * The same animation (e.g. a shared "walk" clip) often drives several
 * characters whose skeletons differ in bone order, naming extras, or both.
 * This builds a name-based map from the source clip's tracks to the target
 * skeleton, then samples through it: every mapped bone is overridden with the
 * clip's value, every unmapped bone keeps the target's bind pose.
 *
 * The remapper owns no data — it stores only the track-to-bone mapping and
 * references the clip passed to Sample, which must outlive the call. Building
 * is explicit so an app can validate a pairing once and reuse it every frame.
 */
class CENGINE_API SkeletonRemapper {
public:
    /**
     * @brief Maps @p sourceClip's tracks onto @p target by bone name.
     *
     * A track whose bone index is out of range for @p source, or whose bone
     * name does not exist in @p target, maps to nothing (that bone stays at
     * its bind pose when sampled).
     * @return false when the clip has no tracks (nothing could be mapped);
     *         the map is still cleared on failure.
     */
    bool Build(const Skeleton& source, const SkeletalClip& sourceClip,
               const Skeleton& target);

    /** True after a successful Build. */
    bool IsValid() const noexcept { return !m_targetBonePerTrack.empty(); }

    /** Number of source tracks that found a target bone (unmapped excluded). */
    std::size_t MappedTracks() const noexcept;

    /**
     * @brief Samples @p sourceClip at @p time onto @p target's bones.
     *
     * @p out is reset to the target's bind pose, then every mapped track
     * overrides its bone's keyed channels. Requires a valid Build and a clip
     * whose track layout matches the one Build was called with.
     */
    void Sample(const SkeletalClip& sourceClip, float time,
                const Skeleton& target, SkeletonPose& out) const;

private:
    /** target bone index per source track index; -1 = unmapped. */
    std::vector<int> m_targetBonePerTrack;
};

} // namespace Concord::Animation

#endif // CONCORD_SKELETONREMAPPER_H
