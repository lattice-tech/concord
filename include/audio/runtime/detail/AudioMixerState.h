#ifndef CONCORD_AUDIOMIXERSTATE_H
#define CONCORD_AUDIOMIXERSTATE_H

#include "audio/effects/AudioDucking.h"
#include "audio/effects/AudioEffectDesc.h"
#include "audio/effects/AudioMixSnapshot.h"
#include "audio/runtime/AudioBus.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Concord::Audio::Detail {

/**
 * @brief Control-plane mix state shared with the render thread.
 *
 * Holds only plain data: gain targets, mute flags, effect descriptions,
 * ducking rules, and named snapshots. The mixer reads it under the render
 * lock and owns every live DSP object itself, rebuilding chains when the
 * revision counters change, so this class stays allocation-free on the
 * steady-state render path.
 */
class AudioMixerState {
public:
    static constexpr std::size_t kBusCount = kAudioBusCount;

    void Reset(bool startMuted);

    void SetBusGain(AudioBusId bus, float gain) noexcept;
    void SetBusMute(AudioBusId bus, bool mute) noexcept;
    float BusGain(AudioBusId bus) const noexcept;
    bool BusMuted(AudioBusId bus) const noexcept;
    float GainFadeSeconds() const noexcept { return m_gainFadeSeconds; }

    void SetBusEffects(AudioBusId bus, std::span<const AudioEffectDesc> descs);
    void ClearBusEffects(AudioBusId bus);
    const std::vector<AudioEffectDesc>& BusEffects(AudioBusId bus) const noexcept;
    std::uint64_t EffectsRevision() const noexcept { return m_effectsRevision; }

    void SetDucking(const AudioDuckingDesc& desc);
    void ClearDucking() noexcept;
    const std::vector<AudioDuckingDesc>& DuckingRules() const noexcept
    {
        return m_duckingRules;
    }
    std::uint64_t DuckingRevision() const noexcept { return m_duckingRevision; }

    void DefineSnapshot(const std::string& name, const AudioMixSnapshotDesc& desc);
    bool RemoveSnapshot(const std::string& name);
    bool ApplySnapshot(const std::string& name, float fadeSeconds);

private:
    static constexpr std::size_t BusIndex(AudioBusId bus) noexcept
    {
        return static_cast<std::size_t>(bus);
    }

    std::array<float, kBusCount> m_busGains{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    std::array<bool, kBusCount> m_busMuted{};
    std::array<std::vector<AudioEffectDesc>, kBusCount> m_busEffects{};
    std::vector<AudioDuckingDesc> m_duckingRules;
    std::unordered_map<std::string, AudioMixSnapshotDesc> m_snapshots;
    std::uint64_t m_effectsRevision = 0;
    std::uint64_t m_duckingRevision = 0;
    float m_gainFadeSeconds = 0.020f;
};

} // namespace Concord::Audio::Detail

#endif // CONCORD_AUDIOMIXERSTATE_H
