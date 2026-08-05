#include "engine/animation/clip/AnimationPlayer.h"

#include "engine/object/Node.h"

#include <cmath>

namespace Concord::Animation {

void AnimationPlayer::Play(const AnimationClip* clip, PlaybackMode mode)
{
    m_clip = clip;
    m_mode = mode;
    m_time = 0.0f;
    m_pingPongReversing = false;
    m_eventSampler.Reset();
    m_playing = clip != nullptr;
    if (m_playing) {
        // Snap to the first pose immediately so a freshly-played clip does not
        // show one frame of the target's old transform before the first Update.
        ApplyPose();
    }
}

void AnimationPlayer::Stop() noexcept
{
    m_clip = nullptr;
    m_playing = false;
    m_time = 0.0f;
    m_pingPongReversing = false;
    m_eventSampler.Reset();
}

void AnimationPlayer::Resume() noexcept
{
    if (m_clip != nullptr) {
        m_playing = true;
    }
}

void AnimationPlayer::Update(float deltaTime)
{
    if (!m_playing || m_clip == nullptr || m_target == nullptr) {
        return;
    }

    const float duration = m_clip->Duration();
    if (duration <= 0.0f) {
        // Zero-length clip: hold the single pose, nothing to advance.
        ApplyPose();
        return;
    }

    const float step = deltaTime * m_speed;
    const bool wasForward = !m_pingPongReversing;
    const float oldTime = m_time;
    m_time += m_pingPongReversing ? -step : step;

    float bounceBoundary = -1.0f;
    switch (m_mode) {
        case PlaybackMode::Once:
            if (m_time >= duration) {
                m_time = duration;
                m_playing = false; // hold final pose, report finished
            } else if (m_time < 0.0f) {
                m_time = 0.0f;
            }
            break;

        case PlaybackMode::Loop:
            // Wrap into [0, duration) with fmod, guarding negative speeds.
            m_time = std::fmod(m_time, duration);
            if (m_time < 0.0f) {
                m_time += duration;
            }
            break;

        case PlaybackMode::PingPong:
            if (m_time >= duration) {
                bounceBoundary = duration;
                m_time = duration - (m_time - duration);
                m_pingPongReversing = true;
            } else if (m_time < 0.0f) {
                bounceBoundary = 0.0f;
                m_time = -m_time;
                m_pingPongReversing = false;
            }
            break;
    }

    ApplyPose();
    FireEvents(oldTime, m_time, bounceBoundary, wasForward, duration, step);
}

void AnimationPlayer::FireEvents(float oldTime, float newTime,
                                 float bounceBoundary, bool wasForward,
                                 float duration, float rawStep)
{
    if (m_clip == nullptr || m_clip->events.Empty()) {
        return;
    }
    if (bounceBoundary >= 0.0f) {
        // A PingPong bounce crosses the boundary twice in one frame: once in
        // the outgoing direction, then again coming back. Deliver both
        // windows; the second starts fresh from the boundary.
        m_eventSampler.Collect(m_clip->events, bounceBoundary, duration,
                               m_mode, wasForward);
        m_eventSampler.SetTime(bounceBoundary);
        m_eventSampler.Collect(m_clip->events, newTime, duration, m_mode,
                               !wasForward);
        return;
    }
    // A loop frame that crosses the wrap and lands exactly on the old time (a
    // whole turn) is invisible to the sampler's from==to window; split it
    // manually. The regular wrap (newTime < oldTime) is handled by the
    // sampler itself.
    const bool crossedWrap = m_mode == PlaybackMode::Loop && duration > 0.0f
        && ((rawStep >= 0.0f && oldTime + rawStep >= duration)
            || (rawStep < 0.0f && oldTime + rawStep <= 0.0f));
    if (crossedWrap) {
        m_eventSampler.Collect(m_clip->events, duration, duration, m_mode,
                               rawStep >= 0.0f);
        m_eventSampler.Reset();
        m_eventSampler.Collect(m_clip->events, newTime, duration, m_mode,
                               rawStep >= 0.0f);
        return;
    }
    m_eventSampler.Collect(m_clip->events, newTime, duration, m_mode,
                           wasForward);
}

void AnimationPlayer::ApplyPose()
{
    if (m_clip == nullptr || m_target == nullptr) {
        return;
    }
    // Only overwrite channels the clip actually keys, so an animation that
    // touches one part of the transform leaves the rest under game control.
    if (!m_clip->position.Empty()) {
        m_target->SetPosition(m_clip->position.Sample(m_time));
    }
    if (!m_clip->rotation.Empty()) {
        m_target->SetRotation(m_clip->rotation.Sample(m_time));
    }
    if (!m_clip->scale.Empty()) {
        m_target->SetScale(m_clip->scale.Sample(m_time));
    }
}

void AnimationPlayer::SetEventCallback(
    std::function<void(const SkeletalEvent&)> callback)
{
    m_eventSampler.ClearCallbacks();
    m_eventSampler.AddCallback(std::move(callback));
}

int AnimationPlayer::CurrentFrame() const
{
    if (m_clip == nullptr || m_clip->sprite.Empty()) {
        return -1;
    }
    return m_clip->sprite.Sample(m_time);
}

} // namespace Concord::Animation
