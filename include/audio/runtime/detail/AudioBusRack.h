#ifndef CONCORD_AUDIOBUSRACK_H
#define CONCORD_AUDIOBUSRACK_H

#include "audio/effects/detail/AudioEffectChain.h"
#include "audio/effects/detail/LimiterEffect.h"
#include "audio/runtime/AudioRuntimeConfig.h"
#include "audio/runtime/detail/AudioStatsBoard.h"
#include "audio/runtime/detail/AudioMixerState.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Concord::Audio::Detail {

/**
 * @brief Owns the per-bus mix buffers and all bus-level DSP state.
 *
 * Voices accumulate into `Buffer(bus)`; `MixDown` then runs each bus's
 * effect chain, applies ducking and ramped bus gains, folds everything
 * into the master bus, runs the master chain, and finally clamps the
 * output through the always-on safety limiter. Chains rebuild lazily
 * when the mixer state's revision counters change.
 */
class AudioBusRack {
public:
    bool Init(const AudioRuntimeConfig& config);
    void Shutdown() noexcept;

    void Begin(std::uint32_t frames) noexcept;
    float* Buffer(AudioBusId bus) noexcept
    {
        return m_busBuffers[static_cast<std::size_t>(bus)].data();
    }

    void MixDown(float* interleavedStereo, std::uint32_t frames,
                 const AudioMixerState& state, AudioStatsBoard& stats);

private:
    static constexpr std::size_t kBusCount = AudioMixerState::kBusCount;

    void SyncConfig(const AudioMixerState& state);
    void UpdateDucking(const AudioMixerState& state, std::uint32_t frames);
    float DuckFactor(const AudioMixerState& state, AudioBusId bus) const noexcept;
    float RampGain(std::size_t busIndex, float target, float fadeSeconds,
                   std::uint32_t frames) noexcept;
    static float PeakLevel(const float* stereo, std::uint32_t frames) noexcept;
    static void AccumulateRamped(const float* source, float* destination,
                                 std::uint32_t frames, float gainBegin,
                                 float gainEnd) noexcept;

    std::array<std::vector<float>, kBusCount> m_busBuffers{};
    std::array<AudioEffectChain, kBusCount> m_busChains{};
    std::array<float, kBusCount> m_currentGains{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    std::array<float, kBusCount> m_busPeaks{};
    std::vector<float> m_duckEnvelopes;
    LimiterEffect m_masterLimiter{AudioLimiterParams{}};
    std::uint64_t m_effectsRevisionSeen = 0;
    std::uint64_t m_duckingRevisionSeen = 0;
    std::uint32_t m_sampleRate = 48000;
    bool m_initialized = false;
};

} // namespace Concord::Audio::Detail

#endif // CONCORD_AUDIOBUSRACK_H
