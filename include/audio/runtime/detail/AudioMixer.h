#ifndef CONCORD_AUDIOMIXER_H
#define CONCORD_AUDIOMIXER_H

#include "audio/runtime/AudioListenerState.h"
#include "audio/runtime/AudioStats.h"
#include "audio/runtime/AudioRuntimeConfig.h"
#include "audio/runtime/detail/AudioClipRegistry.h"
#include "audio/runtime/detail/AudioMixerState.h"
#include "audio/runtime/detail/AudioVoicePool.h"
#include "audio/spatial/SteamAudioSpatializer.h"

#include <cstdint>
#include <vector>

namespace Concord::Audio::Detail {

class AudioMixer {
public:
    bool Init(const AudioRuntimeConfig& config);
    void Shutdown() noexcept;
    bool IsInitialized() const noexcept { return m_initialized; }

    void Render(float* interleavedStereo, std::uint32_t frames,
                const AudioListenerState& listener,
                const AudioClipRegistry& clips, AudioVoicePool& voices,
                const AudioMixerState& buses, AudioStats& stats);

private:
    float EffectivePitch(const ActiveVoiceView& voice,
                         const AudioListenerState& listener) const noexcept;
    float DistanceGain(const AudioSourceState& source,
                       const AudioListenerState& listener) const noexcept;
    float PeakLevel(const float* bus, std::uint32_t frames) const noexcept;
    float SoftLimit(float sample) const noexcept;
    void AccumulateStereo(const float* stereo, std::uint32_t frames,
                          float gain, float* out) noexcept;
    void MixBusToMaster(const float* bus, std::uint32_t frames,
                        float gain, float* out) noexcept;

    std::vector<SteamAudioSpatializer> m_spatializers;
    std::vector<float> m_monoScratch;
    std::vector<float> m_stereoScratch;
    std::vector<float> m_masterScratch;
    std::vector<float> m_musicScratch;
    std::vector<float> m_sfxScratch;
    std::vector<float> m_uiScratch;
    std::vector<float> m_dialogueScratch;
    std::vector<ActiveVoiceView> m_activeVoices;
    AudioRuntimeConfig m_config{};
    bool m_initialized = false;
};

} // namespace Concord::Audio::Detail

#endif // CONCORD_AUDIOMIXER_H
