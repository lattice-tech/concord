#ifndef CONCORD_REVERBEFFECT_H
#define CONCORD_REVERBEFFECT_H

#include "audio/effects/AudioEffectDesc.h"
#include "audio/effects/IAudioEffect.h"

#include <array>
#include <cstddef>
#include <vector>

namespace Concord::Audio::Detail {

/** One damped feedback comb filter line of the Freeverb topology. */
class ReverbCombLine {
public:
    void Configure(std::size_t delaySamples, float feedback, float damping);
    void Clear() noexcept;
    float Process(float input) noexcept;

private:
    std::vector<float> m_buffer;
    std::size_t m_cursor = 0;
    float m_feedback = 0.5f;
    float m_damp1 = 0.5f;
    float m_damp2 = 0.5f;
    float m_filterStore = 0.0f;
};

/** One Schroeder allpass diffusion stage of the Freeverb topology. */
class ReverbAllpassLine {
public:
    void Configure(std::size_t delaySamples);
    void Clear() noexcept;
    float Process(float input) noexcept;

private:
    std::vector<float> m_buffer;
    std::size_t m_cursor = 0;
};

/**
 * @brief Freeverb-style stereo reverb insert effect.
 *
 * Eight damped combs in parallel feed four serial allpass diffusers per
 * channel; the right channel's delay lines are offset by a fixed stereo
 * spread so the tail decorrelates. Delay lengths are tuned for 44.1 kHz
 * and rescaled to the actual device rate at Init.
 */
class ReverbEffect final : public IAudioEffect {
public:
    explicit ReverbEffect(const AudioReverbParams& params) noexcept
        : m_params(params)
    {
    }

    bool Init(std::uint32_t sampleRate) override;
    void Shutdown() noexcept override;
    void Process(float* interleavedStereo, std::uint32_t frames) override;
    void Reset() noexcept override;

private:
    static constexpr std::size_t kCombCount = 8;
    static constexpr std::size_t kAllpassCount = 4;

    AudioReverbParams m_params{};
    float m_wet1 = 0.0f;
    float m_wet2 = 0.0f;
    std::array<ReverbCombLine, kCombCount> m_combLeft{};
    std::array<ReverbCombLine, kCombCount> m_combRight{};
    std::array<ReverbAllpassLine, kAllpassCount> m_allpassLeft{};
    std::array<ReverbAllpassLine, kAllpassCount> m_allpassRight{};
};

} // namespace Concord::Audio::Detail

#endif // CONCORD_REVERBEFFECT_H
