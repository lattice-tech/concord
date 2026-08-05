#ifndef CONCORD_SKELETALSTATE_H
#define CONCORD_SKELETALSTATE_H

#include "engine/animation/state/AnimationParameters.h"
#include "engine/animation/clip/PlaybackMode.h"
#include "engine/animation/blend/SkeletalBlend.h"
#include "engine/animation/clip/SkeletalClip.h"
#include "engine/animation/skeleton/Skeleton.h"

#include <string>

namespace Concord::Animation {

/**
 * One node in a SkeletalStateMachine: produces a whole-skeleton pose over time
 * from either a single SkeletalClip or a 1D skeletal blend space driven by a
 * named float parameter. Referenced clip/blend-space memory is owned by the
 * caller (typically the SkinnedModel's imported clips) and must outlive the
 * state machine. Time is owned by the machine; the state just samples.
 */
struct SkeletalState {
    std::string name;

    /** Single-clip source (mutually exclusive with `blendSpace`). */
    const SkeletalClip* clip = nullptr;

    /** Blend-space source (mutually exclusive with `clip`). */
    const SkeletalBlendSpace1D* blendSpace = nullptr;

    /** Parameter driving the blend space's axis; ignored for clip states. */
    std::string blendParameter;

    PlaybackMode mode = PlaybackMode::Loop;
    float speed = 1.0f;

    /** This state's timeline length (clip or blend-space duration). */
    float Duration() const noexcept
    {
        if (clip != nullptr) {
            return clip->Duration();
        }
        if (blendSpace != nullptr) {
            return blendSpace->Duration();
        }
        return 0.0f;
    }

    /** Samples the state's pose at local time @p time over @p skeleton into @p out. */
    void Sample(float time, const Skeleton& skeleton, const AnimationParameters& params,
                SkeletonPose& out) const
    {
        if (clip != nullptr) {
            clip->Sample(time, skeleton, out);
        } else if (blendSpace != nullptr) {
            const float duration = blendSpace->Duration();
            const float phase = duration > 1e-6f ? time / duration : 0.0f;
            blendSpace->Sample(params.GetFloat(blendParameter), phase, skeleton, out);
        } else {
            out = skeleton.BindPose();
        }
    }
};

} // namespace Concord::Animation

#endif // CONCORD_SKELETALSTATE_H
