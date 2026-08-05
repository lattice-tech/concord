#ifndef CONCORD_SKELETALCLIP_H
#define CONCORD_SKELETALCLIP_H

#include "Concord/CExport.h"
#include "engine/animation/clip/AnimationTrack.h"
#include "engine/animation/clip/SkeletalEventTrack.h"
#include "engine/animation/clip/SyncTrack.h"
#include "engine/animation/skeleton/Skeleton.h"
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

    /**
     * Named timeline markers fired as playback crosses them (footstep on the
     * left foot, impact frame of an attack). Pure data; SkeletalEventSampler
     * delivers them to instance-level callbacks.
     */
    SkeletalEventTrack events;

    /**
     * Named alignment markers used by blend spaces and crossfades to line up
     * footfalls across clips of different lengths (see SyncTrack).
     */
    SyncTrack sync;

    /**
     * Bone whose animated world motion becomes *root motion*: its translation
     * and rotation are delivered as deltas for the character's node to apply,
     * instead of deforming the mesh (the bone is reset to its bind pose in the
     * skinning pose). -1 (default) disables root motion.
     */
    int rootBone = -1;

    /**
     * @brief Computes the root bone's motion between two playback times.
     *
     * Both deltas are in the skeleton's model space: the position delta is the
     * root bone's world translation difference, the rotation delta the
     * relative rotation `q(from)^-1 * q(to)`. A wrap (toTime < fromTime, a
     * loop crossing the end) is split into its tail and head segments and the
     * deltas combined.
     * @return false when root motion is disabled or @p rootBone is out of
     *         range for @p skeleton; the outputs are left untouched.
     */
    CENGINE_API bool SampleRootMotion(float fromTime, float toTime,
                                      const Skeleton& skeleton,
                                      Vector3& outPositionDelta,
                                      Quaternion& outRotationDelta) const;
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
