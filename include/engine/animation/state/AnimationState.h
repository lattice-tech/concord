#ifndef CONCORD_ANIMATIONSTATE_H
#define CONCORD_ANIMATIONSTATE_H

#include "engine/animation/clip/AnimationClip.h"
#include "engine/animation/state/AnimationParameters.h"
#include "engine/animation/blend/BlendSpace1D.h"
#include "engine/animation/clip/PlaybackMode.h"
#include "engine/animation/blend/Pose.h"

#include <string>

namespace Concord::Animation {

/**
 * One node in the state machine's graph. A state produces a pose over time
 * from exactly one source: either a single clip, or a 1D blend space driven by
 * a named float parameter. Referenced clip/blend-space memory is owned by the
 * caller and must outlive the state machine.
 *
 * The state itself is stateless with respect to time — the state machine owns
 * the playback clock and asks the state to sample at a given local time, so
 * the same state definition can be re-entered cleanly.
 */
struct AnimationState {
    std::string name;

    /** Single-clip source (mutually exclusive with `blendSpace`). */
    const AnimationClip* clip = nullptr;

    /** Blend-space source (mutually exclusive with `clip`). */
    const BlendSpace1D* blendSpace = nullptr;

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

    /**
     * Samples the state's pose at local time @p time (already wrapped/clamped
     * by the state machine per `mode`). Blend states map time to a normalised
     * phase and mix by the current value of `blendParameter`.
     */
    Pose Sample(float time, const AnimationParameters& params) const
    {
        if (clip != nullptr) {
            return clip->SamplePose(time);
        }
        if (blendSpace != nullptr) {
            const float duration = blendSpace->Duration();
            const float phase = duration > 1e-6f ? time / duration : 0.0f;
            return blendSpace->Sample(params.GetFloat(blendParameter), phase);
        }
        return Pose{};
    }
};

} // namespace Concord::Animation

#endif // CONCORD_ANIMATIONSTATE_H
