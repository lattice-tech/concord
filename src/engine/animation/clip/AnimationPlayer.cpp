#include "engine/animation/AnimationPlayer.h"

#include "engine/object/Node.h"

#include <cmath>

namespace Concord::Animation {

void AnimationPlayer::Play(const AnimationClip* clip, PlaybackMode mode)
{
    m_clip = clip;
    m_mode = mode;
    m_time = 0.0f;
    m_pingPongReversing = false;
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
    m_time += m_pingPongReversing ? -step : step;

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
                m_time = duration - (m_time - duration);
                m_pingPongReversing = true;
            } else if (m_time < 0.0f) {
                m_time = -m_time;
                m_pingPongReversing = false;
            }
            break;
    }

    ApplyPose();
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

int AnimationPlayer::CurrentFrame() const
{
    if (m_clip == nullptr || m_clip->sprite.Empty()) {
        return -1;
    }
    return m_clip->sprite.Sample(m_time);
}

} // namespace Concord::Animation
