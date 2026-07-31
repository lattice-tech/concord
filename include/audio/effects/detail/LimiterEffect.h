#ifndef CONCORD_LIMITEREFFECT_H
#define CONCORD_LIMITEREFFECT_H

#include "audio/effects/AudioEffectDesc.h"
#include "audio/effects/IAudioEffect.h"

namespace Concord::Audio::Detail {

/**
 * @brief Brick-wall peak limiter with instant attack and smooth release.
 *
 * Gain drops immediately when a peak would exceed the ceiling and recovers
 * with the release time constant, so the master bus can never clip no
 * matter how many voices stack up.
 */
class LimiterEffect final : public IAudioEffect {
public:
    explicit LimiterEffect(const AudioLimiterParams& params) noexcept
        : m_params(params)
    {
    }

    bool Init(std::uint32_t sampleRate) override;
    void Shutdown() noexcept override;
    void Process(float* interleavedStereo, std::uint32_t frames) override;
    void Reset() noexcept override;

private:
    AudioLimiterParams m_params{};
    float m_releaseCoefficient = 0.0f;
    float m_gain = 1.0f;
};

} // namespace Concord::Audio::Detail

#endif // CONCORD_LIMITEREFFECT_H
