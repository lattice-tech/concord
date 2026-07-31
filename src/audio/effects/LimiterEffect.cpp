#include "audio/effects/detail/LimiterEffect.h"

#include <algorithm>
#include <cmath>

namespace Concord::Audio::Detail {

bool LimiterEffect::Init(std::uint32_t sampleRate)
{
    if (sampleRate == 0) {
        return false;
    }
    m_params.ceiling = std::isfinite(m_params.ceiling)
        ? std::clamp(m_params.ceiling, 0.05f, 1.0f) : 0.98f;
    const float releaseSeconds = std::isfinite(m_params.releaseSeconds)
        ? std::max(m_params.releaseSeconds, 0.001f) : 0.080f;
    m_releaseCoefficient = std::exp(-1.0f / (releaseSeconds
        * static_cast<float>(sampleRate)));
    m_gain = 1.0f;
    return true;
}

void LimiterEffect::Shutdown() noexcept
{
    m_gain = 1.0f;
}

void LimiterEffect::Process(float* interleavedStereo, std::uint32_t frames)
{
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        float& left = interleavedStereo[frame * 2u];
        float& right = interleavedStereo[frame * 2u + 1u];
        if (!std::isfinite(left)) {
            left = 0.0f;
        }
        if (!std::isfinite(right)) {
            right = 0.0f;
        }
        const float peak = std::max(std::abs(left), std::abs(right));
        const float required = peak > m_params.ceiling ? m_params.ceiling / peak : 1.0f;
        if (required < m_gain) {
            m_gain = required;
        } else {
            m_gain = m_releaseCoefficient * m_gain
                + (1.0f - m_releaseCoefficient) * required;
        }
        const float gain = std::min(m_gain, 1.0f);
        left *= gain;
        right *= gain;
    }
}

void LimiterEffect::Reset() noexcept
{
    m_gain = 1.0f;
}

} // namespace Concord::Audio::Detail
