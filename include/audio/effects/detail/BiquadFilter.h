#ifndef CONCORD_BIQUADFILTER_H
#define CONCORD_BIQUADFILTER_H

#include <cstdint>

namespace Concord::Audio::Detail {

/**
 * @brief Direct-form-I stereo biquad with RBJ cookbook coefficients.
 *
 * The two channels share one coefficient set but keep independent history,
 * so a single instance filters an interleaved stereo stream in place.
 */
class BiquadFilter {
public:
    void ConfigureLowPass(std::uint32_t sampleRate, float cutoffHz, float q) noexcept;
    void ConfigureHighPass(std::uint32_t sampleRate, float cutoffHz, float q) noexcept;
    void Reset() noexcept;
    void ProcessInterleavedStereo(float* samples, std::uint32_t frames) noexcept;

private:
    struct History {
        float x1 = 0.0f;
        float x2 = 0.0f;
        float y1 = 0.0f;
        float y2 = 0.0f;
    };

    float ProcessSample(float input, History& history) noexcept;

    float m_b0 = 1.0f;
    float m_b1 = 0.0f;
    float m_b2 = 0.0f;
    float m_a1 = 0.0f;
    float m_a2 = 0.0f;
    History m_left{};
    History m_right{};
};

} // namespace Concord::Audio::Detail

#endif // CONCORD_BIQUADFILTER_H
