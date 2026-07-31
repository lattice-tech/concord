#include "audio/runtime/detail/AudioMixerState.h"

#include <algorithm>
#include <cmath>

namespace Concord::Audio::Detail {

void AudioMixerState::Reset(bool startMuted)
{
    m_busGains.fill(1.0f);
    m_busMuted.fill(false);
    for (auto& effects : m_busEffects) {
        effects.clear();
    }
    m_duckingRules.clear();
    m_snapshots.clear();
    ++m_effectsRevision;
    ++m_duckingRevision;
    m_gainFadeSeconds = 0.020f;
    if (startMuted) {
        m_busMuted[BusIndex(AudioBusId::Master)] = true;
    }
}

void AudioMixerState::SetBusGain(AudioBusId bus, float gain) noexcept
{
    m_busGains[BusIndex(bus)] = gain;
    m_gainFadeSeconds = 0.020f;
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

void AudioMixerState::SetBusEffects(AudioBusId bus,
                                    std::span<const AudioEffectDesc> descs)
{
    std::vector<AudioEffectDesc>& effects = m_busEffects[BusIndex(bus)];
    effects.assign(descs.begin(), descs.end());
    ++m_effectsRevision;
}

void AudioMixerState::ClearBusEffects(AudioBusId bus)
{
    std::vector<AudioEffectDesc>& effects = m_busEffects[BusIndex(bus)];
    if (effects.empty()) {
        return;
    }
    effects.clear();
    ++m_effectsRevision;
}

const std::vector<AudioEffectDesc>& AudioMixerState::BusEffects(AudioBusId bus) const noexcept
{
    return m_busEffects[BusIndex(bus)];
}

void AudioMixerState::SetDucking(const AudioDuckingDesc& desc)
{
    const auto matches = [&desc](const AudioDuckingDesc& rule) {
        return rule.trigger == desc.trigger && rule.target == desc.target;
    };
    const auto found = std::find_if(m_duckingRules.begin(), m_duckingRules.end(), matches);
    if (found != m_duckingRules.end()) {
        *found = desc;
    } else {
        m_duckingRules.push_back(desc);
    }
    ++m_duckingRevision;
}

void AudioMixerState::ClearDucking() noexcept
{
    if (m_duckingRules.empty()) {
        return;
    }
    m_duckingRules.clear();
    ++m_duckingRevision;
}

void AudioMixerState::DefineSnapshot(const std::string& name,
                                     const AudioMixSnapshotDesc& desc)
{
    AudioMixSnapshotDesc sanitized = desc;
    for (float& gain : sanitized.busGains) {
        gain = std::isfinite(gain) ? std::max(gain, 0.0f) : 1.0f;
    }
    m_snapshots[name] = sanitized;
}

bool AudioMixerState::RemoveSnapshot(const std::string& name)
{
    return m_snapshots.erase(name) != 0;
}

bool AudioMixerState::ApplySnapshot(const std::string& name, float fadeSeconds)
{
    const auto found = m_snapshots.find(name);
    if (found == m_snapshots.end()) {
        return false;
    }
    m_busGains = found->second.busGains;
    m_gainFadeSeconds = std::isfinite(fadeSeconds)
        ? std::clamp(fadeSeconds, 0.0f, 60.0f) : 0.0f;
    return true;
}

} // namespace Concord::Audio::Detail
