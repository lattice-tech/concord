#include "audio/effects/detail/FilterEffect.h"

namespace Concord::Audio::Detail {

bool LowPassEffect::Init(std::uint32_t sampleRate)
{
    if (sampleRate == 0) {
        return false;
    }
    m_filter.ConfigureLowPass(sampleRate, m_params.cutoffHz, m_params.q);
    m_filter.Reset();
    return true;
}

void LowPassEffect::Shutdown() noexcept
{
    m_filter.Reset();
}

void LowPassEffect::Process(float* interleavedStereo, std::uint32_t frames)
{
    m_filter.ProcessInterleavedStereo(interleavedStereo, frames);
}

void LowPassEffect::Reset() noexcept
{
    m_filter.Reset();
}

bool HighPassEffect::Init(std::uint32_t sampleRate)
{
    if (sampleRate == 0) {
        return false;
    }
    m_filter.ConfigureHighPass(sampleRate, m_params.cutoffHz, m_params.q);
    m_filter.Reset();
    return true;
}

void HighPassEffect::Shutdown() noexcept
{
    m_filter.Reset();
}

void HighPassEffect::Process(float* interleavedStereo, std::uint32_t frames)
{
    m_filter.ProcessInterleavedStereo(interleavedStereo, frames);
}

void HighPassEffect::Reset() noexcept
{
    m_filter.Reset();
}

} // namespace Concord::Audio::Detail
