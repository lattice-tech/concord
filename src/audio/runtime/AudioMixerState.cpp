#include "audio/runtime/detail/AudioMixerState.h"

namespace Concord::Audio::Detail {

void AudioMixerState::Reset(bool startMuted) noexcept
{
    m_busGains.fill(1.0f);
    m_busMuted.fill(false);
    if (startMuted) {
        m_busMuted[BusIndex(AudioBusId::Master)] = true;
    }
}

void AudioMixerState::SetBusGain(AudioBusId bus, float gain) noexcept
{
    m_busGains[BusIndex(bus)] = gain;
}

void AudioMixerState::SetBusMute(AudioBusId bus, bool mute) noexcept
{
    m_busMuted[BusIndex(bus)] = mute;
}

float AudioMixerState::BusGain(AudioBusId bus) const noexcept
{
    return m_busGains[BusIndex(bus)];
}

bool AudioMixerState::BusMuted(AudioBusId bus) const noexcept
{
    return m_busMuted[BusIndex(bus)];
}

} // namespace Concord::Audio::Detail
