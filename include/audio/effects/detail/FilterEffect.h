#ifndef CONCORD_FILTEREFFECT_H
#define CONCORD_FILTEREFFECT_H

#include "audio/effects/AudioEffectDesc.h"
#include "audio/effects/IAudioEffect.h"
#include "audio/effects/detail/BiquadFilter.h"

namespace Concord::Audio::Detail {

/** Second-order low-pass insert effect. */
class LowPassEffect final : public IAudioEffect {
public:
    explicit LowPassEffect(const AudioFilterParams& params) noexcept
        : m_params(params)
    {
    }

    bool Init(std::uint32_t sampleRate) override;
    void Shutdown() noexcept override;
    void Process(float* interleavedStereo, std::uint32_t frames) override;
    void Reset() noexcept override;

private:
    AudioFilterParams m_params{};
    BiquadFilter m_filter{};
};

/** Second-order high-pass insert effect. */
class HighPassEffect final : public IAudioEffect {
public:
    explicit HighPassEffect(const AudioFilterParams& params) noexcept
        : m_params(params)
    {
    }

    bool Init(std::uint32_t sampleRate) override;
    void Shutdown() noexcept override;
    void Process(float* interleavedStereo, std::uint32_t frames) override;
    void Reset() noexcept override;

private:
    AudioFilterParams m_params{};
    BiquadFilter m_filter{};
};

} // namespace Concord::Audio::Detail

#endif // CONCORD_FILTEREFFECT_H
