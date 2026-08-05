#include "engine/animation/layer/SkeletalLayer.h"

#include <algorithm>
#include <utility>

namespace Concord::Animation {
namespace {

/** Wraps/clamps a playback time per the layer's mode. */
float WrapTime(float time, float duration, PlaybackMode mode)
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

} // namespace

void SkeletalLayer::SetClip(const SkeletalClip* clip, PlaybackMode mode,
                            float speed)
{
    m_clip = clip;
    m_blendSpace = nullptr;
    m_blendParameter.clear();
    m_mode = mode;
    m_speed = speed;
    Reset();
}

void SkeletalLayer::SetBlendSpace(const SkeletalBlendSpace1D* blendSpace,
                                  std::string blendParameter, PlaybackMode mode,
                                  float speed)
{
    m_clip = nullptr;
    m_blendSpace = blendSpace;
    m_blendParameter = std::move(blendParameter);
    m_mode = mode;
    m_speed = speed;
    Reset();
}

void SkeletalLayer::Reset()
{
    m_time = 0.0f;
    m_sampler.Reset();
    m_pingPongReversing = false;
}

void SkeletalLayer::Sample(float deltaTime, const Skeleton& skeleton,
                           const AnimationParameters& params, SkeletonPose& out)
{
    const float duration = m_clip != nullptr
        ? m_clip->Duration()
        : (m_blendSpace != nullptr ? m_blendSpace->Duration() : 0.0f);
    const float oldTime = m_time;
    const float step = deltaTime * m_speed;
    if (m_clip != nullptr) {
        float bounceBoundary = -1.0f;
        if (m_mode == PlaybackMode::PingPong && duration > 0.0f) {
            // Track the bounce explicitly so the frame that turns around can
            // deliver the events crossed in both directions; the folding
            // WrapTime cannot.
            m_time += m_pingPongReversing ? -step : step;
            if (m_time >= duration) {
                bounceBoundary = duration;
                m_time = duration - (m_time - duration);
                m_pingPongReversing = true;
            } else if (m_time < 0.0f) {
                bounceBoundary = 0.0f;
                m_time = -m_time;
                m_pingPongReversing = false;
            }
        } else {
            m_time = WrapTime(oldTime + step, duration, m_mode);
        }
        m_clip->Sample(m_time, skeleton, out);
        if (bounceBoundary >= 0.0f) {
            const bool reachedEnd = bounceBoundary >= duration;
            m_sampler.Collect(m_clip->events, bounceBoundary, duration, m_mode,
                              reachedEnd);
            m_sampler.SetTime(bounceBoundary);
            m_sampler.Collect(m_clip->events, m_time, duration, m_mode,
                              !reachedEnd);
        } else if (m_mode == PlaybackMode::Loop && duration > 0.0f
                   && ((step >= 0.0f && oldTime + step >= duration)
                       || (step < 0.0f && oldTime + step <= 0.0f))) {
            // A loop frame that crosses the wrap and lands exactly on the old
            // time (a whole turn) is invisible to the sampler's from==to
            // window; split it manually.
            m_sampler.Collect(m_clip->events, duration, duration, m_mode,
                              step >= 0.0f);
            m_sampler.Reset();
            m_sampler.Collect(m_clip->events, m_time, duration, m_mode,
                              step >= 0.0f);
        } else {
            m_sampler.Collect(m_clip->events, m_time, duration, m_mode,
                              step >= 0.0f);
        }
    } else if (m_blendSpace != nullptr) {
        m_time = WrapTime(oldTime + step, duration, m_mode);
        const float phase = duration > 1e-6f ? m_time / duration : 0.0f;
        m_blendSpace->Sample(params.GetFloat(m_blendParameter), phase, skeleton, out);
    } else {
        m_time = 0.0f;
        out = skeleton.BindPose();
    }
}

void SkeletalLayer::SetEventCallback(
    std::function<void(const SkeletalEvent&)> callback)
{
    m_sampler.ClearCallbacks();
    m_sampler.AddCallback(std::move(callback));
}

} // namespace Concord::Animation
