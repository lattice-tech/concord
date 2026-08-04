#include "engine/animation/AnimStateMachine.h"

#include "engine/object/Node.h"

#include <cmath>

namespace Concord::Animation {

void AnimStateMachine::AddState(const std::string& name, const AnimationClip* clip,
                                PlaybackMode mode, float speed)
{
    AnimationState state;
    state.name = name;
    state.clip = clip;
    state.mode = mode;
    state.speed = speed;
    m_states.push_back(std::move(state));
}

void AnimStateMachine::AddBlendState(const std::string& name, const BlendSpace1D* blendSpace,
                                     const std::string& blendParameter, PlaybackMode mode, float speed)
{
    AnimationState state;
    state.name = name;
    state.blendSpace = blendSpace;
    state.blendParameter = blendParameter;
    state.mode = mode;
    state.speed = speed;
    m_states.push_back(std::move(state));
}

void AnimStateMachine::AddTransition(const std::string& from, const std::string& to,
                                     std::vector<TransitionCondition> conditions, float duration)
{
    AnimationTransition transition;
    transition.from = from;
    transition.to = to;
    transition.conditions = std::move(conditions);
    transition.duration = duration;
    m_transitions.push_back(std::move(transition));
}

void AnimStateMachine::SetEntry(const std::string& name)
{
    m_current = FindState(name);
    m_currentTime = 0.0f;
    m_transitioning = false;
    m_next = -1;
}

int AnimStateMachine::FindState(const std::string& name) const
{
    for (std::size_t i = 0; i < m_states.size(); ++i) {
        if (m_states[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

float AnimStateMachine::WrapTime(float time, float duration, PlaybackMode mode)
{
    if (duration <= 0.0f) {
        return 0.0f;
    }
    switch (mode) {
        case PlaybackMode::Once:
            return time < 0.0f ? 0.0f : (time > duration ? duration : time);
        case PlaybackMode::Loop: {
            float t = std::fmod(time, duration);
            if (t < 0.0f) {
                t += duration;
            }
            return t;
        }
        case PlaybackMode::PingPong: {
            const float period = 2.0f * duration;
            float t = std::fmod(time, period);
            if (t < 0.0f) {
                t += period;
            }
            return t <= duration ? t : period - t;
        }
    }
    return time;
}

int AnimStateMachine::PickTransition(int stateIndex)
{
    if (stateIndex < 0) {
        return -1;
    }
    const std::string& stateName = m_states[stateIndex].name;
    for (std::size_t i = 0; i < m_transitions.size(); ++i) {
        const AnimationTransition& tr = m_transitions[i];
        // Match source: named transitions only from that state; empty `from`
        // is an "any state" transition. Never transition into the same state.
        if (!tr.from.empty() && tr.from != stateName) {
            continue;
        }
        if (tr.to == stateName) {
            continue;
        }
        bool allPass = true;
        for (const TransitionCondition& c : tr.conditions) {
            if (!c.Evaluate(m_params)) {
                allPass = false;
                break;
            }
        }
        if (allPass) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void AnimStateMachine::Update(float deltaTime)
{
    if (m_current < 0 || m_target == nullptr || m_states.empty()) {
        return;
    }

    // Evaluate transitions only when not already crossfading, so a fade runs to
    // completion before the next one can start (keeps blending predictable).
    if (!m_transitioning) {
        const int trIndex = PickTransition(m_current);
        if (trIndex >= 0) {
            const AnimationTransition& tr = m_transitions[trIndex];
            const int dest = FindState(tr.to);
            if (dest >= 0) {
                // Consume any triggers this transition relied on so they fire once.
                for (const TransitionCondition& c : tr.conditions) {
                    if (c.IsTrigger()) {
                        m_params.ConsumeTrigger(c.parameter);
                    }
                }
                if (tr.duration <= 0.0f) {
                    // Instant cut.
                    m_current = dest;
                    m_currentTime = 0.0f;
                } else {
                    m_transitioning = true;
                    m_next = dest;
                    m_nextTime = 0.0f;
                    m_blendElapsed = 0.0f;
                    m_blendDuration = tr.duration;
                }
            }
        }
    }

    // Advance the active state clock.
    AnimationState& cur = m_states[m_current];
    m_currentTime = WrapTime(m_currentTime + deltaTime * cur.speed, cur.Duration(), cur.mode);
    Pose pose = cur.Sample(m_currentTime, m_params);

    if (m_transitioning && m_next >= 0) {
        AnimationState& nxt = m_states[m_next];
        m_nextTime = WrapTime(m_nextTime + deltaTime * nxt.speed, nxt.Duration(), nxt.mode);
        m_blendElapsed += deltaTime;
        const float t = m_blendDuration > 1e-6f ? m_blendElapsed / m_blendDuration : 1.0f;
        const Pose nextPose = nxt.Sample(m_nextTime, m_params);
        if (t >= 1.0f) {
            // Fade complete: the incoming state becomes current.
            m_current = m_next;
            m_currentTime = m_nextTime;
            m_transitioning = false;
            m_next = -1;
            pose = nextPose;
        } else {
            pose = BlendPose(pose, nextPose, t);
        }
    }

    m_lastFrame = pose.spriteFrame;
    WriteToTarget(pose);
}

void AnimStateMachine::WriteToTarget(const Pose& pose)
{
    if (m_target == nullptr) {
        return;
    }
    if (pose.hasPosition) {
        m_target->SetPosition(pose.position);
    }
    if (pose.hasRotation) {
        m_target->SetRotation(pose.rotation);
    }
    if (pose.hasScale) {
        m_target->SetScale(pose.scale);
    }
}

const std::string& AnimStateMachine::CurrentState() const
{
    static const std::string kNone;
    const int index = m_transitioning ? m_next : m_current;
    if (index < 0 || index >= static_cast<int>(m_states.size())) {
        return kNone;
    }
    return m_states[index].name;
}

} // namespace Concord::Animation
