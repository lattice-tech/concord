#ifndef CONCORD_SKELETALCLIP_H
#define CONCORD_SKELETALCLIP_H

#include "engine/animation/AnimationTrack.h"
#include "engine/animation/Skeleton.h"
#include "math/Quaternion.h"
#include "math/Vector3.h"

#include <algorithm>
#include <string>
#include <vector>

namespace Concord::Animation {

/**
 * The keyed local-transform channels for one bone within a skeletal clip. Any
 * channel may be empty (that bone doesn't animate on it, keeping its bind
 * value). Mirrors a glTF animation's per-node translation/rotation/scale
 * samplers.
 */
struct BoneTrack {
    int boneIndex = -1;
    AnimationTrack<Vector3> position;
    AnimationTrack<Quaternion> rotation;
    AnimationTrack<Vector3> scale;
};

/**
 * A whole-skeleton animation: per-bone transform tracks sampled into a
 * SkeletonPose. This is the skeletal analogue of AnimationClip (which targets
 * a single node); a glTF animation becomes one of these, its channels routed
 * to bone tracks by joint index.
 *
 * Sampling seeds the pose from the skeleton's bind pose, then overrides only
 * the channels a bone actually keys — so a clip that animates only the arms
 * leaves the legs in their rest pose.
 */
struct SkeletalClip {
    std::string name;
    float length = 0.0f;
    std::vector<BoneTrack> tracks;

    /** Effective length: explicit `length`, or the longest keyed channel. */
    float Duration() const noexcept
    {
        float d = length;
        for (const BoneTrack& t : tracks) {
            d = std::max(d, t.position.Duration());
            d = std::max(d, t.rotation.Duration());
            d = std::max(d, t.scale.Duration());
        }
        return d;
    }

    /**
     * Samples the clip at @p time into @p pose over @p skeleton: the pose is
     * reset to the bind pose, then every bone track overrides its keyed
     * channels. `pose` is (re)sized to the skeleton's bone count.
     */
    void Sample(float time, const Skeleton& skeleton, SkeletonPose& pose) const
    {
        pose = skeleton.BindPose();
        const int boneCount = static_cast<int>(pose.local.size());
        for (const BoneTrack& track : tracks) {
            if (track.boneIndex < 0 || track.boneIndex >= boneCount) {
                continue;
            }
            Transform& local = pose.local[static_cast<std::size_t>(track.boneIndex)];
            if (!track.position.Empty()) {
                local.position = track.position.Sample(time);
            }
            if (!track.rotation.Empty()) {
                local.rotation = track.rotation.Sample(time);
            }
            if (!track.scale.Empty()) {
                local.scale = track.scale.Sample(time);
            }
        }
    }
};

} // namespace Concord::Animation

#endif // CONCORD_SKELETALCLIP_H
