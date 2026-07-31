#ifndef CONCORD_AUDIOEFFECTDESC_H
#define CONCORD_AUDIOEFFECTDESC_H

#include <cstdint>

namespace Concord::Audio {

/** Kind of insert effect instantiated on a bus effect chain. */
enum class AudioEffectType : std::uint8_t {
    LowPass = 0,
    HighPass,
    Compressor,
    Reverb,
    Limiter,
};

/** Cutoff/resonance settings shared by the low-pass and high-pass filters. */
struct AudioFilterParams {
    float cutoffHz = 8000.0f;
    float q = 0.70710678f;
};

/** Feed-forward dynamics compressor settings. */
struct AudioCompressorParams {
    float thresholdDb = -18.0f;
    float ratio = 4.0f;
    float attackSeconds = 0.005f;
    float releaseSeconds = 0.120f;
    float makeupDb = 0.0f;
};

/** Freeverb-style reverb settings; wet/dry are linear gains. */
struct AudioReverbParams {
    float roomSize = 0.5f;
    float damping = 0.5f;
    float width = 1.0f;
    float wet = 0.30f;
    float dry = 1.0f;
};

/** Brick-wall peak limiter settings; ceiling is a linear amplitude. */
struct AudioLimiterParams {
    float ceiling = 0.98f;
    float releaseSeconds = 0.080f;
};

/**
 * @brief Plain-data description of one insert effect on a bus chain.
 *
 * Only the member matching `type` is read; the others keep their defaults.
 * Descriptions cross the public API by value so callers never touch the
 * mixer-thread effect instances built from them.
 */
struct AudioEffectDesc {
    AudioEffectType type = AudioEffectType::LowPass;
    AudioFilterParams filter{};
    AudioCompressorParams compressor{};
    AudioReverbParams reverb{};
    AudioLimiterParams limiter{};
};

/** Small indoor room: short, bright tail. */
inline AudioEffectDesc MakeRoomReverb() noexcept
{
    AudioEffectDesc desc{};
    desc.type = AudioEffectType::Reverb;
    desc.reverb = {.roomSize = 0.45f, .damping = 0.55f, .width = 1.0f,
                   .wet = 0.22f, .dry = 1.0f};
    return desc;
}

/** Large cave: long, dark tail. */
inline AudioEffectDesc MakeCaveReverb() noexcept
{
    AudioEffectDesc desc{};
    desc.type = AudioEffectType::Reverb;
    desc.reverb = {.roomSize = 0.88f, .damping = 0.25f, .width = 1.0f,
                   .wet = 0.42f, .dry = 1.0f};
    return desc;
}

/** Open water: sparse, wide early reflections with little tail. */
inline AudioEffectDesc MakeOpenSeaReverb() noexcept
{
    AudioEffectDesc desc{};
    desc.type = AudioEffectType::Reverb;
    desc.reverb = {.roomSize = 0.60f, .damping = 0.75f, .width = 1.0f,
                   .wet = 0.12f, .dry = 1.0f};
    return desc;
}

} // namespace Concord::Audio

#endif // CONCORD_AUDIOEFFECTDESC_H
