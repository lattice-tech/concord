#ifndef CONCORD_SKELETALSTATEMACHINE_H
#define CONCORD_SKELETALSTATEMACHINE_H

#include "Concord/CExport.h"
#include "engine/animation/AnimationParameters.h"
#include "engine/animation/AnimationTransition.h"
#include "engine/animation/PlaybackMode.h"
#include "engine/animation/SkeletalBlend.h"
#include "engine/animation/SkeletalState.h"
#include "engine/animation/Skeleton.h"

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
                       std::vector<TransitionCondition> conditions, float duration = 0.15f);

    /** Sets the state the machine starts in. */
    void SetEntry(const std::string& name);

    AnimationParameters& Parameters() noexcept { return m_params; }
    const AnimationParameters& Parameters() const noexcept { return m_params; }

    /** Advances the machine and applies the blended pose to the target SkinnedModel. */
    void Update(float deltaTime);

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

    // Scratch poses reused across frames to avoid per-frame allocation.
    SkeletonPose m_poseA;
    SkeletonPose m_poseB;
    SkeletonPose m_blended;
};

} // namespace Animation
} // namespace Concord

#endif // CONCORD_SKELETALSTATEMACHINE_H
