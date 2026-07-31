#include "audio/effects/detail/ReverbEffect.h"

#include <algorithm>
#include <cmath>

namespace Concord::Audio::Detail {

namespace {

constexpr std::size_t kCombTuning[8] = {1116, 1188, 1277, 1356,
                                        1422, 1491, 1557, 1617};
constexpr std::size_t kAllpassTuning[4] = {556, 441, 341, 225};
constexpr std::size_t kStereoSpread = 23;
constexpr float kTuningRate = 44100.0f;
constexpr float kFixedGain = 0.015f;

std::size_t ScaleDelay(std::size_t samplesAt44k, std::uint32_t sampleRate) noexcept
{
    const float scaled = static_cast<float>(samplesAt44k)
        * static_cast<float>(sampleRate) / kTuningRate;
    return std::max<std::size_t>(1, static_cast<std::size_t>(scaled + 0.5f));
}

float Undenormal(float value) noexcept
{
    return std::abs(value) < 1.0e-18f ? 0.0f : value;
}

} // namespace

void ReverbCombLine::Configure(std::size_t delaySamples, float feedback, float damping)
{
    m_buffer.assign(std::max<std::size_t>(1, delaySamples), 0.0f);
    m_cursor = 0;
    m_feedback = feedback;
    m_damp1 = damping;
    m_damp2 = 1.0f - damping;
    m_filterStore = 0.0f;
}

void ReverbCombLine::Clear() noexcept
{
    std::fill(m_buffer.begin(), m_buffer.end(), 0.0f);
    m_filterStore = 0.0f;
    m_cursor = 0;
}

float ReverbCombLine::Process(float input) noexcept
{
    const float output = m_buffer[m_cursor];
    m_filterStore = Undenormal(output * m_damp2 + m_filterStore * m_damp1);
    m_buffer[m_cursor] = Undenormal(input + m_filterStore * m_feedback);
    m_cursor = (m_cursor + 1) % m_buffer.size();
    return output;
}

void ReverbAllpassLine::Configure(std::size_t delaySamples)
{
    m_buffer.assign(std::max<std::size_t>(1, delaySamples), 0.0f);
    m_cursor = 0;
}

void ReverbAllpassLine::Clear() noexcept
{
    std::fill(m_buffer.begin(), m_buffer.end(), 0.0f);
    m_cursor = 0;
}

float ReverbAllpassLine::Process(float input) noexcept
{
    const float buffered = m_buffer[m_cursor];
    const float output = buffered - input;
    m_buffer[m_cursor] = Undenormal(input + buffered * 0.5f);
    m_cursor = (m_cursor + 1) % m_buffer.size();
    return output;
}

bool ReverbEffect::Init(std::uint32_t sampleRate)
{
    if (sampleRate == 0) {
        return false;
    }
    m_params.roomSize = std::isfinite(m_params.roomSize)
        ? std::clamp(m_params.roomSize, 0.0f, 1.0f) : 0.5f;
    m_params.damping = std::isfinite(m_params.damping)
        ? std::clamp(m_params.damping, 0.0f, 1.0f) : 0.5f;
    m_params.width = std::isfinite(m_params.width)
        ? std::clamp(m_params.width, 0.0f, 1.0f) : 1.0f;
    m_params.wet = std::isfinite(m_params.wet)
        ? std::clamp(m_params.wet, 0.0f, 3.0f) : 0.3f;
    m_params.dry = std::isfinite(m_params.dry)
        ? std::clamp(m_params.dry, 0.0f, 2.0f) : 1.0f;

    const float feedback = 0.70f + m_params.roomSize * 0.28f;
    const float damping = m_params.damping * 0.4f;
    for (std::size_t index = 0; index < kCombCount; ++index) {
        m_combLeft[index].Configure(ScaleDelay(kCombTuning[index], sampleRate),
                                    feedback, damping);
        m_combRight[index].Configure(
            ScaleDelay(kCombTuning[index] + kStereoSpread, sampleRate),
            feedback, damping);
    }
    for (std::size_t index = 0; index < kAllpassCount; ++index) {
        m_allpassLeft[index].Configure(ScaleDelay(kAllpassTuning[index], sampleRate));
        m_allpassRight[index].Configure(
            ScaleDelay(kAllpassTuning[index] + kStereoSpread, sampleRate));
    }
    m_wet1 = m_params.wet * (m_params.width * 0.5f + 0.5f);
    m_wet2 = m_params.wet * ((1.0f - m_params.width) * 0.5f);
    return true;
}

void ReverbEffect::Shutdown() noexcept
{
    Reset();
}

void ReverbEffect::Process(float* interleavedStereo, std::uint32_t frames)
{
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        float& left = interleavedStereo[frame * 2u];
        float& right = interleavedStereo[frame * 2u + 1u];
        const float input = (left + right) * kFixedGain;
        float outLeft = 0.0f;
        float outRight = 0.0f;
        for (std::size_t index = 0; index < kCombCount; ++index) {
            outLeft += m_combLeft[index].Process(input);
            outRight += m_combRight[index].Process(input);
        }
        for (std::size_t index = 0; index < kAllpassCount; ++index) {
            outLeft = m_allpassLeft[index].Process(outLeft);
            outRight = m_allpassRight[index].Process(outRight);
        }
        left = outLeft * m_wet1 + outRight * m_wet2 + left * m_params.dry;
        right = outRight * m_wet1 + outLeft * m_wet2 + right * m_params.dry;
    }
}

void ReverbEffect::Reset() noexcept
{
    for (auto& comb : m_combLeft) {
        comb.Clear();
    }
    for (auto& comb : m_combRight) {
        comb.Clear();
    }
    for (auto& allpass : m_allpassLeft) {
        allpass.Clear();
    }
    for (auto& allpass : m_allpassRight) {
        allpass.Clear();
    }
}

} // namespace Concord::Audio::Detail
