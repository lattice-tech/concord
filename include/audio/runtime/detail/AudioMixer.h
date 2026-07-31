#ifndef CONCORD_AUDIOMIXER_H
#define CONCORD_AUDIOMIXER_H

#include "audio/runtime/AudioListenerState.h"
#include "audio/runtime/detail/AudioStatsBoard.h"
#include "audio/runtime/AudioRuntimeConfig.h"
#include "audio/runtime/detail/AudioBusRack.h"
#include "audio/runtime/detail/AudioClipRegistry.h"
#include "audio/runtime/detail/AudioMixerState.h"
#include "audio/runtime/detail/AudioVoicePool.h"
#include "audio/spatial/SteamAudioSpatializer.h"

#include <cstdint>
#include <unordered_map>
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
                const AudioMixerState& buses, AudioStatsBoard& stats);

private:
    /** One-pole low-pass history for one occluded voice. */
    struct OcclusionState {
        float left = 0.0f;
    };

    float EffectivePitch(const ActiveVoiceView& voice,
                         const AudioListenerState& listener) const noexcept;
    float DistanceGain(const AudioSourceState& source,
                       const AudioListenerState& listener) const noexcept;
    float ApplyOcclusion(std::uint64_t voiceKey, float occlusion,
                         float* mono, std::uint32_t frames) noexcept;
    void AccumulateStereo(const float* stereo, std::uint32_t frames,
                          float gain, float* out) noexcept;

    std::vector<SteamAudioSpatializer> m_spatializers;
    std::vector<float> m_monoScratch;
    std::vector<float> m_stereoScratch;
    std::vector<ActiveVoiceView> m_activeVoices;
    /**
     * Stable voice -> spatializer assignment. HRTF effects carry per-voice
     * convolution history, so a voice must keep the same spatializer across
     * frames even when distance sorting reorders the active list; reassigning
     * by traversal order made histories switch owners, audible as pops.
     */
    std::unordered_map<std::uint64_t, std::uint32_t> m_spatialAssignments;
    std::unordered_map<std::uint64_t, OcclusionState> m_occlusionStates;
    std::vector<bool> m_spatialSlotUsed;
    AudioBusRack m_busRack;
    AudioRuntimeConfig m_config{};
    bool m_initialized = false;
};

} // namespace Concord::Audio::Detail

#endif // CONCORD_AUDIOMIXER_H
