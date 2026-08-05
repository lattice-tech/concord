#ifndef CONCORD_SKELETALSTATEMACHINE_H
#define CONCORD_SKELETALSTATEMACHINE_H

#include "Concord/CExport.h"
#include "engine/animation/state/AnimationParameters.h"
#include "engine/animation/state/AnimationTransition.h"
#include "engine/animation/clip/PlaybackMode.h"
#include "engine/animation/clip/SkeletalEventSampler.h"
#include "engine/animation/clip/SyncTrack.h"
#include "engine/animation/blend/SkeletalBlend.h"
#include "engine/animation/state/SkeletalState.h"
#include "engine/animation/skeleton/Skeleton.h"
#include "engine/motion/Easing.h"

#include <functional>
#include <string>
#include <vector>

namespace Concord {
namespace Object {
class SkinnedModel;
}

namespace Animation {

/**
 * A skeletal animation state machine: the skinned-character analogue of
 * AnimStateMachine. States are skeletal clips or blend spaces; parameter-driven
 * transitions cross-fade between whole-skeleton poses; the blended pose is
 * applied to a SkinnedModel each Update.
 *
 * It reuses the shared parameter/transition types (AnimationParameters,
 * TransitionCondition, AnimationTransition, PlaybackMode). Drive it through
 * Parameters() and call Update(dt) each frame; it samples the active (and,
 * mid-crossfade, incoming) state, blends bone-by-bone, and calls
 * SkinnedModel::ApplyPose — which takes over from the model's own clip playback.
 *
 * ```
 *   SkeletalBlendSpace1D loco;                 // clips live in the SkinnedModel
 *   loco.AddClip(0, fox.FindClip("Survey"));   // idle
 *   loco.AddClip(3, fox.FindClip("Walk"));
 *   loco.AddClip(6, fox.FindClip("Run"));
 *   SkeletalStateMachine sm;
 *   sm.SetTarget(&fox);
 *   sm.AddBlendState("move", &loco, "speed");
 *   sm.SetEntry("move");
 *   // per frame: sm.Parameters().SetFloat("speed", v); sm.Update(dt);
 * ```
 */
class CENGINE_API SkeletalStateMachine {
public:
    /** The skinned model whose pose is driven; also supplies the skeleton. */
    void SetTarget(Object::SkinnedModel* model) noexcept { m_target = model; }

    /** Adds a single-clip state (clip owned elsewhere, e.g. the SkinnedModel). */
    void AddState(const std::string& name, const SkeletalClip* clip,
                  PlaybackMode mode = PlaybackMode::Loop, float speed = 1.0f);

    /** Adds a blend-space state driven by the float parameter @p blendParameter. */
    void AddBlendState(const std::string& name, const SkeletalBlendSpace1D* blendSpace,
                       const std::string& blendParameter,
                       PlaybackMode mode = PlaybackMode::Loop, float speed = 1.0f);

    /** Adds a transition (empty @p from = any state); conditions ANDed. */
    void AddTransition(const std::string& from, const std::string& to,
                       std::vector<TransitionCondition> conditions,
                       float duration = 0.15f,
                       Motion::Easing blendEasing = Motion::Easing::Linear,
                       std::string syncName = {});

    /** Sets the state the machine starts in. */
    void SetEntry(const std::string& name);

    AnimationParameters& Parameters() noexcept { return m_params; }
    const AnimationParameters& Parameters() const noexcept { return m_params; }

    /** Advances the machine and applies the blended pose to the target SkinnedModel. */
    void Update(float deltaTime);

    /**
     * @brief Advances the machine and samples the blended pose into @p out.
     *
     * The same evaluation as Update, but without applying the result to the
     * target model — used by SkeletalAnimator, which composes layers on top.
     * Requires a target (it supplies the skeleton); nothing is written to it.
     */
    void Sample(float deltaTime, SkeletonPose& out);

    /**
     * @brief Registers a callback for the current single-clip state's events.
     *
     * Markers on the state's clip fire as the machine's clock crosses them
     * (honouring direction and wrap; see SkeletalEventSampler). Blend-space
     * states fire nothing. Only one callback is stored.
     */
    void SetEventCallback(std::function<void(const SkeletalEvent&)> callback);

    /**
     * @brief Programmatically switches to @p name, bypassing conditions.
     *
     * With @p crossfadeSeconds <= 0 the switch is instant; otherwise the
     * machine crossfades to the new state over that duration (used by
     * actions that jump straight to an attack state, e.g. from game code).
     * @return false when no state has that name; the machine is unchanged.
     */
    bool SetState(const std::string& name, float crossfadeSeconds = 0.0f);

    /** Name of the current state (the crossfade target while transitioning). */
    const std::string& CurrentState() const;

    bool IsTransitioning() const noexcept { return m_transitioning; }

private:
    int FindState(const std::string& name) const;
    int PickTransition(int stateIndex);
    static float WrapTime(float time, float duration, PlaybackMode mode);

    std::vector<SkeletalState> m_states;
    std::vector<AnimationTransition> m_transitions;
    AnimationParameters m_params;
    Object::SkinnedModel* m_target = nullptr;

    int m_current = -1;
    float m_currentTime = 0.0f;

    bool m_transitioning = false;
    int m_next = -1;
    float m_nextTime = 0.0f;
    float m_blendElapsed = 0.0f;
    float m_blendDuration = 0.0f;
    /** Blend curve and sync marker of the transition currently fading. */
    Motion::Easing m_transitionEasing = Motion::Easing::Linear;
    std::string m_transitionSyncName;
    SkeletalEventSampler m_eventSampler;
    /** Direction of the current state's PingPong playback (reset on switch). */
    bool m_pingPongReversing = false;

    // Scratch poses reused across frames to avoid per-frame allocation.
    SkeletonPose m_poseA;
    SkeletonPose m_poseB;
    SkeletonPose m_blended;
};

} // namespace Animation
} // namespace Concord

#endif // CONCORD_SKELETALSTATEMACHINE_H
