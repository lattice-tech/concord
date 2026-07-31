#ifndef CONCORD_COMPRESSOREFFECT_H
#define CONCORD_COMPRESSOREFFECT_H

#include "audio/effects/AudioEffectDesc.h"
#include "audio/effects/IAudioEffect.h"

namespace Concord::Audio::Detail {

/**
 * @brief Feed-forward peak compressor with a shared stereo detector.
 *
 * The detector follows the louder channel so the stereo image never
 * shifts; gain reduction is computed in decibels and smoothed with
 * separate attack and release one-pole coefficients.
 */
class CompressorEffect final : public IAudioEffect {
public:
    explicit CompressorEffect(const AudioCompressorParams& params) noexcept
        : m_params(params)
    {
    }

    bool Init(std::uint32_t sampleRate) override;
    void Shutdown() noexcept override;
    void Process(float* interleavedStereo, std::uint32_t frames) override;
    void Reset() noexcept override;

private:
    AudioCompressorParams m_params{};
    float m_attackCoefficient = 0.0f;
    float m_releaseCoefficient = 0.0f;
    float m_makeupGain = 1.0f;
    float m_envelopeDb = 0.0f;
};

} // namespace Concord::Audio::Detail

#endif // CONCORD_COMPRESSOREFFECT_H
