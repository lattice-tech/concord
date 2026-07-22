#ifndef CONCORD_ANIMSTATEMACHINE_H
#define CONCORD_ANIMSTATEMACHINE_H

#include "Concord/CExport.h"
#include "engine/animation/AnimationParameters.h"
#include "engine/animation/AnimationState.h"
#include "engine/animation/AnimationTransition.h"
#include "engine/animation/BlendSpace1D.h"
#include "engine/animation/PlaybackMode.h"
#include "engine/animation/Pose.h"

#include <string>
#include <vector>

namespace Concord {
namespace Object {
class Node;
}

namespace Animation {

/**
 * A 2D/3D-agnostic animation state machine: named states (each a clip or a
 * blend space), parameter-driven transitions with crossfades, and a target
 * node whose local transform receives the blended pose each Update.
 *
 * This is L3 of the animation roadmap, built on the L2 clip layer. Game logic
 * drives the machine only through Parameters() (set a speed float, flip an
 * isGrounded bool, fire a jump trigger); the machine decides when to leave a
 * state, cross-fades between the outgoing and incoming poses over the
 * transition's duration, and writes the result to the node — the same code
 * path for a 3D character's TRS and a 2D sprite's frame index (CurrentFrame()).
 *
 * States and transitions are declared once up front. Referenced clips and
 * blend spaces are not owned and must outlive the machine.
 *
 * ```
 *   AnimStateMachine sm;
 *   sm.SetTarget(&node);
 *   sm.AddState("idle", &idleClip);
 *   sm.AddBlendState("move", &locomotion, "speed");
 *   sm.AddTransition("idle", "move", {{"speed", TransitionCondition::Op::Greater, 0.1f}}, 0.2f);
 *   sm.AddTransition("move", "idle", {{"speed", TransitionCondition::Op::Less, 0.1f}}, 0.2f);
 *   sm.SetEntry("idle");
 *   // per frame: sm.Parameters().SetFloat("speed", v); sm.Update(dt);
 * ```
 */
class CENGINE_API AnimStateMachine {
public:
    /** The node whose local transform the blended pose is written to each Update. */
    void SetTarget(Object::Node* node) noexcept { m_target = node; }

    /** Adds a single-clip state. */
    void AddState(const std::string& name, const AnimationClip* clip,
                  PlaybackMode mode = PlaybackMode::Loop, float speed = 1.0f);

    /** Adds a blend-space state driven by the float parameter @p blendParameter. */
    void AddBlendState(const std::string& name, const BlendSpace1D* blendSpace,
                       const std::string& blendParameter,
                       PlaybackMode mode = PlaybackMode::Loop, float speed = 1.0f);

    /**
     * Adds a transition. @p from empty makes it an "any state" transition.
     * Conditions are ANDed; @p duration is the crossfade length in seconds.
     */
    void AddTransition(const std::string& from, const std::string& to,
                       std::vector<TransitionCondition> conditions, float duration = 0.15f);

    /** Sets the state the machine starts (and resets) in. */
    void SetEntry(const std::string& name);

    /** Mutable parameter store the game drives and transitions read. */
    AnimationParameters& Parameters() noexcept { return m_params; }
    const AnimationParameters& Parameters() const noexcept { return m_params; }

    /**
     * Advances the machine by @p deltaTime: evaluates transitions, advances the
     * active (and, mid-crossfade, incoming) state clock, blends the poses and
     * writes the result to the target's local transform.
     */
    void Update(float deltaTime);

    /** Name of the state currently playing (the crossfade *target* while transitioning). */
    const std::string& CurrentState() const;

    /** True while a crossfade between two states is in progress. */
    bool IsTransitioning() const noexcept { return m_transitioning; }

    /** Active 2D sprite frame index of the blended pose, or -1 if none. */
    int CurrentFrame() const noexcept { return m_lastFrame; }

private:
    int FindState(const std::string& name) const;
    void WriteToTarget(const Pose& pose);
    /** Wraps/clamps @p time into a state's timeline per its PlaybackMode. */
    static float WrapTime(float time, float duration, PlaybackMode mode);
    /** Picks the first transition out of @p stateIndex whose conditions all pass. */
    int PickTransition(int stateIndex);

    std::vector<AnimationState> m_states;
    std::vector<AnimationTransition> m_transitions;
    AnimationParameters m_params;
    Object::Node* m_target = nullptr;

    int m_current = -1;
    float m_currentTime = 0.0f;

    bool m_transitioning = false;
    int m_next = -1;
    float m_nextTime = 0.0f;
    float m_blendElapsed = 0.0f;
    float m_blendDuration = 0.0f;

    int m_lastFrame = -1;
};

} // namespace Animation
} // namespace Concord

#endif // CONCORD_ANIMSTATEMACHINE_H
