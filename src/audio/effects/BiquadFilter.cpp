#include "audio/effects/detail/BiquadFilter.h"

#include <algorithm>
#include <cmath>

namespace Concord::Audio::Detail {

namespace {

constexpr float kPi = 3.14159265358979323846f;

float ClampCutoff(std::uint32_t sampleRate, float cutoffHz) noexcept
{
    const float nyquist = static_cast<float>(sampleRate) * 0.5f;
    if (!std::isfinite(cutoffHz)) {
        return nyquist * 0.5f;
    }
    return std::clamp(cutoffHz, 10.0f, nyquist * 0.99f);
}

float ClampQ(float q) noexcept
{
    if (!std::isfinite(q)) {
        return 0.70710678f;
    }
    return std::clamp(q, 0.05f, 18.0f);
}

} // namespace

void BiquadFilter::ConfigureLowPass(std::uint32_t sampleRate, float cutoffHz, float q) noexcept
{
    const float omega = 2.0f * kPi * ClampCutoff(sampleRate, cutoffHz)
        / static_cast<float>(sampleRate);
    const float sinOmega = std::sin(omega);
    const float cosOmega = std::cos(omega);
    const float alpha = sinOmega / (2.0f * ClampQ(q));
    const float a0 = 1.0f + alpha;
    const float inverseA0 = 1.0f / a0;
    m_b0 = ((1.0f - cosOmega) * 0.5f) * inverseA0;
    m_b1 = (1.0f - cosOmega) * inverseA0;
    m_b2 = m_b0;
    m_a1 = (-2.0f * cosOmega) * inverseA0;
    m_a2 = (1.0f - alpha) * inverseA0;
}

void BiquadFilter::ConfigureHighPass(std::uint32_t sampleRate, float cutoffHz, float q) noexcept
{
    const float omega = 2.0f * kPi * ClampCutoff(sampleRate, cutoffHz)
        / static_cast<float>(sampleRate);
    const float sinOmega = std::sin(omega);
    const float cosOmega = std::cos(omega);
    const float alpha = sinOmega / (2.0f * ClampQ(q));
    const float a0 = 1.0f + alpha;
    const float inverseA0 = 1.0f / a0;
    m_b0 = ((1.0f + cosOmega) * 0.5f) * inverseA0;
    m_b1 = (-(1.0f + cosOmega)) * inverseA0;
    m_b2 = m_b0;
    m_a1 = (-2.0f * cosOmega) * inverseA0;
    m_a2 = (1.0f - alpha) * inverseA0;
}

void BiquadFilter::Reset() noexcept
{
    m_left = {};
    m_right = {};
}

void BiquadFilter::ProcessInterleavedStereo(float* samples, std::uint32_t frames) noexcept
{
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        samples[frame * 2u] = ProcessSample(samples[frame * 2u], m_left);
        samples[frame * 2u + 1u] = ProcessSample(samples[frame * 2u + 1u], m_right);
    }
}

float BiquadFilter::ProcessSample(float input, History& history) noexcept
{
    const float output = m_b0 * input + m_b1 * history.x1 + m_b2 * history.x2
        - m_a1 * history.y1 - m_a2 * history.y2;
    history.x2 = history.x1;
    history.x1 = input;
    history.y2 = history.y1;
    history.y1 = std::isfinite(output) ? output : 0.0f;
    return history.y1;
}

} // namespace Concord::Audio::Detail
