#include "engine/animation/SkeletalStateMachine.h"

#include "engine/object/SkinnedModel.h"

#include <cmath>
#include <utility>

namespace Concord::Animation {

void SkeletalStateMachine::AddState(const std::string& name, const SkeletalClip* clip,
                                    PlaybackMode mode, float speed)
{
    SkeletalState state;
    state.name = name;
    state.clip = clip;
    state.mode = mode;
    state.speed = speed;
    m_states.push_back(std::move(state));
}

void SkeletalStateMachine::AddBlendState(const std::string& name, const SkeletalBlendSpace1D* blendSpace,
                                         const std::string& blendParameter, PlaybackMode mode, float speed)
{
    SkeletalState state;
    state.name = name;
    state.blendSpace = blendSpace;
    state.blendParameter = blendParameter;
    state.mode = mode;
    state.speed = speed;
    m_states.push_back(std::move(state));
}

void SkeletalStateMachine::AddTransition(const std::string& from, const std::string& to,
                                         std::vector<TransitionCondition> conditions, float duration)
{
    AnimationTransition transition;
    transition.from = from;
    transition.to = to;
    transition.conditions = std::move(conditions);
    transition.duration = duration;
    m_transitions.push_back(std::move(transition));
}

void SkeletalStateMachine::SetEntry(const std::string& name)
{
    m_current = FindState(name);
    m_currentTime = 0.0f;
    m_transitioning = false;
    m_next = -1;
}

int SkeletalStateMachine::FindState(const std::string& name) const
{
    for (std::size_t i = 0; i < m_states.size(); ++i) {
        if (m_states[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

float SkeletalStateMachine::WrapTime(float time, float duration, PlaybackMode mode)
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

int SkeletalStateMachine::PickTransition(int stateIndex)
{
    if (stateIndex < 0) {
        return -1;
    }
    const std::string& stateName = m_states[stateIndex].name;
    for (std::size_t i = 0; i < m_transitions.size(); ++i) {
        const AnimationTransition& tr = m_transitions[i];
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

void SkeletalStateMachine::Update(float deltaTime)
{
    if (m_current < 0 || m_target == nullptr || m_states.empty()) {
        return;
    }
    const Skeleton& skeleton = m_target->Skeleton();
    if (skeleton.Empty()) {
        return;
    }

    if (!m_transitioning) {
        const int trIndex = PickTransition(m_current);
        if (trIndex >= 0) {
            const AnimationTransition& tr = m_transitions[trIndex];
            const int dest = FindState(tr.to);
            if (dest >= 0) {
                for (const TransitionCondition& c : tr.conditions) {
                    if (c.IsTrigger()) {
                        m_params.ConsumeTrigger(c.parameter);
                    }
                }
                if (tr.duration <= 0.0f) {
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

    const SkeletalState& cur = m_states[m_current];
    m_currentTime = WrapTime(m_currentTime + deltaTime * cur.speed, cur.Duration(), cur.mode);
    cur.Sample(m_currentTime, skeleton, m_params, m_poseA);

    if (m_transitioning && m_next >= 0) {
        const SkeletalState& nxt = m_states[m_next];
        m_nextTime = WrapTime(m_nextTime + deltaTime * nxt.speed, nxt.Duration(), nxt.mode);
        m_blendElapsed += deltaTime;
        const float t = m_blendDuration > 1e-6f ? m_blendElapsed / m_blendDuration : 1.0f;
        nxt.Sample(m_nextTime, skeleton, m_params, m_poseB);
        if (t >= 1.0f) {
            m_current = m_next;
            m_currentTime = m_nextTime;
            m_transitioning = false;
            m_next = -1;
            m_target->ApplyPose(m_poseB);
            return;
        }
        BlendSkeletonPose(m_poseA, m_poseB, t, m_blended);
        m_target->ApplyPose(m_blended);
        return;
    }

    m_target->ApplyPose(m_poseA);
}

const std::string& SkeletalStateMachine::CurrentState() const
{
    static const std::string kNone;
    const int index = m_transitioning ? m_next : m_current;
    if (index < 0 || index >= static_cast<int>(m_states.size())) {
        return kNone;
    }
    return m_states[index].name;
}

} // namespace Concord::Animation
