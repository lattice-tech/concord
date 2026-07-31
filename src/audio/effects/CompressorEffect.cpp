#include "audio/effects/detail/CompressorEffect.h"

#include <algorithm>
#include <cmath>

namespace Concord::Audio::Detail {

namespace {

float SmoothingCoefficient(std::uint32_t sampleRate, float seconds) noexcept
{
    const float clamped = std::max(seconds, 0.0001f);
    return std::exp(-1.0f / (clamped * static_cast<float>(sampleRate)));
}

float DbToLinear(float db) noexcept
{
    return std::pow(10.0f, db / 20.0f);
}

float LinearToDb(float linear) noexcept
{
    return 20.0f * std::log10(std::max(linear, 1.0e-6f));
}

} // namespace

bool CompressorEffect::Init(std::uint32_t sampleRate)
{
    if (sampleRate == 0) {
        return false;
    }
    m_params.ratio = std::isfinite(m_params.ratio)
        ? std::clamp(m_params.ratio, 1.0f, 40.0f) : 4.0f;
    m_params.thresholdDb = std::isfinite(m_params.thresholdDb)
        ? std::clamp(m_params.thresholdDb, -60.0f, 0.0f) : -18.0f;
    m_attackCoefficient = SmoothingCoefficient(sampleRate, m_params.attackSeconds);
    m_releaseCoefficient = SmoothingCoefficient(sampleRate, m_params.releaseSeconds);
    m_makeupGain = std::isfinite(m_params.makeupDb)
        ? DbToLinear(std::clamp(m_params.makeupDb, -24.0f, 24.0f)) : 1.0f;
    m_envelopeDb = 0.0f;
    return true;
}

void CompressorEffect::Shutdown() noexcept
{
    m_envelopeDb = 0.0f;
}

void CompressorEffect::Process(float* interleavedStereo, std::uint32_t frames)
{
    const float slope = 1.0f - 1.0f / m_params.ratio;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        float& left = interleavedStereo[frame * 2u];
        float& right = interleavedStereo[frame * 2u + 1u];
        const float peak = std::max(std::abs(left), std::abs(right));
        const float overshootDb = std::max(0.0f, LinearToDb(peak) - m_params.thresholdDb);
        const float targetReductionDb = overshootDb * slope;
        const float coefficient = targetReductionDb > m_envelopeDb
            ? m_attackCoefficient : m_releaseCoefficient;
        m_envelopeDb = coefficient * m_envelopeDb
            + (1.0f - coefficient) * targetReductionDb;
        const float gain = DbToLinear(-m_envelopeDb) * m_makeupGain;
        left *= gain;
        right *= gain;
    }
}

void CompressorEffect::Reset() noexcept
{
    m_envelopeDb = 0.0f;
}

} // namespace Concord::Audio::Detail
