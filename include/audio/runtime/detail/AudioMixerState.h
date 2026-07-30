#ifndef CONCORD_AUDIOMIXERSTATE_H
#define CONCORD_AUDIOMIXERSTATE_H

#include "audio/runtime/AudioBus.h"

#include <array>
#include <cstddef>

namespace Concord::Audio::Detail {

class AudioMixerState {
public:
    static constexpr std::size_t kBusCount = 5;

    void Reset(bool startMuted) noexcept;
    void SetBusGain(AudioBusId bus, float gain) noexcept;
    void SetBusMute(AudioBusId bus, bool mute) noexcept;
    float BusGain(AudioBusId bus) const noexcept;
    bool BusMuted(AudioBusId bus) const noexcept;

private:
    static constexpr std::size_t BusIndex(AudioBusId bus) noexcept
    {
        return static_cast<std::size_t>(bus);
    }

    std::array<float, kBusCount> m_busGains{1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    std::array<bool, kBusCount> m_busMuted{false, false, false, false, false};
};

} // namespace Concord::Audio::Detail

#endif // CONCORD_AUDIOMIXERSTATE_H
