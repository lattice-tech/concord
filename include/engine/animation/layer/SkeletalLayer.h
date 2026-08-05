#ifndef CONCORD_SKELETALLAYER_H
#define CONCORD_SKELETALLAYER_H

#include "Concord/CExport.h"
#include "engine/animation/blend/SkeletalBlend.h"
#include "engine/animation/clip/PlaybackMode.h"
#include "engine/animation/clip/SkeletalEventSampler.h"
#include "engine/animation/layer/SkeletalLayerMask.h"
#include "engine/animation/state/AnimationParameters.h"

#include <string>

namespace Concord::Animation {

/**
 * @brief One animation layer: a clip (or 1D blend space) with its own clock,
 * weight, bone mask and normal/additive mode.
 *
 * Layers stack on top of a base pose in the SkeletalAnimator: normal layers
 * blend their masked bones toward the layer's pose, additive layers add their
 * pose-vs-bind difference (aim offsets, breathing) on top. Each layer owns its
 * playback clock and event sampler, so layer clips can fire their own
 * footsteps independently of the base locomotion.
 */
class CENGINE_API SkeletalLayer {
public:
    /** Plays @p clip from the beginning; nullptr stops the layer. */
    void SetClip(const SkeletalClip* clip,
                 PlaybackMode mode = PlaybackMode::Loop, float speed = 1.0f);

    /** Drives the layer from a 1D blend space controlled by @p blendParameter. */
    void SetBlendSpace(const SkeletalBlendSpace1D* blendSpace,
                       std::string blendParameter,
                       PlaybackMode mode = PlaybackMode::Loop, float speed = 1.0f);

    /** Restricts the layer to the masked bones (empty = full body). */
    void SetMask(SkeletalLayerMask mask) noexcept { m_mask = std::move(mask); }

    /** Blend weight in [0, 1]; 0 makes the layer invisible. */
    void SetWeight(float weight) noexcept { m_weight = weight; }

    /** True while the layer adds pose-vs-bind instead of blending in. */
    void SetAdditive(bool additive) noexcept { m_additive = additive; }

    /** Playback rate multiplier. */
    void SetSpeed(float speed) noexcept { m_speed = speed; }

    /** Rewinds the clock and the event sampler. */
    void Reset();

    /**
     * @brief Advances the layer by @p deltaTime and samples it.
     *
     * The layer pose always covers the whole skeleton; the mask is applied by
     * the SkeletalAnimator during composition.
     */
    void Sample(float deltaTime, const Skeleton& skeleton,
                const AnimationParameters& params, SkeletonPose& out);

    /** Current playback time on the layer's own clock. */
    float Time() const noexcept { return m_time; }

    float Weight() const noexcept { return m_weight; }
    bool Additive() const noexcept { return m_additive; }
    const SkeletalLayerMask& Mask() const noexcept { return m_mask; }

    /** Registers the event callback for this layer's clip markers. */
    void SetEventCallback(std::function<void(const SkeletalEvent&)> callback);

private:
    const SkeletalClip* m_clip = nullptr;
    const SkeletalBlendSpace1D* m_blendSpace = nullptr;
    std::string m_blendParameter;
    PlaybackMode m_mode = PlaybackMode::Loop;
    float m_speed = 1.0f;
    float m_weight = 1.0f;
    bool m_additive = false;
    float m_time = 0.0f;
    SkeletalLayerMask m_mask;
    SkeletalEventSampler m_sampler;
    /** Direction of PingPong playback (reset by SetClip/SetBlendSpace/Reset). */
    bool m_pingPongReversing = false;
};

} // namespace Concord::Animation

#endif // CONCORD_SKELETALLAYER_H
