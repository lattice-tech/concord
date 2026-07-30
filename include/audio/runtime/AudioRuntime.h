#ifndef CONCORD_AUDIORUNTIME_H
#define CONCORD_AUDIORUNTIME_H

#include "Concord/CExport.h"
#include "audio/runtime/AudioBus.h"
#include "audio/runtime/AudioClip.h"
#include "audio/runtime/AudioListenerState.h"
#include "audio/runtime/AudioPlayback.h"
#include "audio/runtime/AudioRuntimeConfig.h"
#include "audio/runtime/AudioStats.h"
#include "audio/runtime/AudioVoice.h"
#include "audio/synth/AudioBuiltInSound.h"
#include "audio/synth/AudioSynth.h"

#include <memory>
#include <span>

namespace Concord::Audio {

/**
 * @brief High-level runtime audio facade owned by CAudio.dll.
 *
 * The initial implementation is intentionally minimal: it establishes the
 * stable public API, generation-safe handles, and configuration surface the
 * later SDL-device/mixer/Steam-Audio-backed runtime will grow behind.
 */
class CAUDIO_API AudioRuntime {
public:
    AudioRuntime();
    ~AudioRuntime();

    AudioRuntime(const AudioRuntime&) = delete;
    AudioRuntime& operator=(const AudioRuntime&) = delete;
    AudioRuntime(AudioRuntime&& other) noexcept;
    AudioRuntime& operator=(AudioRuntime&& other) noexcept;

    bool Init(const AudioRuntimeConfig& config = {});
    void Shutdown() noexcept;
    bool IsInitialized() const noexcept;
    void Pump() noexcept;

    AudioClipHandle CreateClipFromPcm(const AudioClipDesc& desc,
                                      std::span<const float> samples);
    AudioClipHandle LoadWavClip(const std::string& path);
    void DestroyClip(AudioClipHandle clip);
    AudioVoiceHandle PlayOneShotPcm(const AudioClipDesc& desc,
                                    std::span<const float> samples,
                                    const AudioPlayParams& params = {});
    AudioVoiceHandle PlayLoopPcm(const AudioClipDesc& desc,
                                 std::span<const float> samples,
                                 const AudioPlayParams& params = {});
    AudioVoiceHandle PlaySpatialOneShotPcm(const AudioClipDesc& desc,
                                           std::span<const float> samples,
                                           const AudioSourceState& source,
                                           const AudioPlayParams& params = {});
    AudioVoiceHandle PlaySpatialLoopPcm(const AudioClipDesc& desc,
                                        std::span<const float> samples,
                                        const AudioSourceState& source,
                                        const AudioPlayParams& params = {});
    AudioClipHandle CreateSynthClip(const AudioSynthesisDesc& desc);
    AudioClipHandle CreateBuiltInClip(AudioBuiltInSound sound,
                                      std::int32_t sampleRate = 48000);
    AudioVoiceHandle PlaySynthOneShot(const AudioSynthesisDesc& desc,
                                      const AudioPlayParams& params = {});
    AudioVoiceHandle PlaySynthLoop(const AudioSynthesisDesc& desc,
                                   const AudioPlayParams& params = {});
    AudioVoiceHandle PlaySynthAt(const AudioSynthesisDesc& desc, const Vector3& position,
                                 const AudioPlayParams& params = {});
    AudioVoiceHandle PlaySynthLoopAt(const AudioSynthesisDesc& desc, const Vector3& position,
                                     const AudioPlayParams& params = {});
    AudioVoiceHandle PlayBuiltInOneShot(AudioBuiltInSound sound,
                                        const AudioPlayParams& params = {});
    AudioVoiceHandle PlayBuiltInLoop(AudioBuiltInSound sound,
                                     const AudioPlayParams& params = {});
    AudioVoiceHandle PlayBuiltInAt(AudioBuiltInSound sound, const Vector3& position,
                                   const AudioPlayParams& params = {});
    AudioVoiceHandle PlayBuiltInLoopAt(AudioBuiltInSound sound, const Vector3& position,
                                       const AudioPlayParams& params = {});

    AudioVoiceHandle PlayOneShot(AudioClipHandle clip,
                                 const AudioPlayParams& params = {});
    AudioVoiceHandle PlayLoop(AudioClipHandle clip,
                              const AudioPlayParams& params = {});
    AudioVoiceHandle PlaySpatialOneShot(AudioClipHandle clip,
                                        const AudioSourceState& source,
                                        const AudioPlayParams& params = {});
    AudioVoiceHandle PlaySpatialLoop(AudioClipHandle clip,
                                     const AudioSourceState& source,
                                     const AudioPlayParams& params = {});
    AudioVoiceHandle PlayAt(AudioClipHandle clip, const Vector3& position,
                            const AudioPlayParams& params = {});
    AudioVoiceHandle PlayLoopAt(AudioClipHandle clip, const Vector3& position,
                                const AudioPlayParams& params = {});
    void StopVoice(AudioVoiceHandle voice);
    void StopAllVoices(AudioBusId bus);
    bool PauseVoice(AudioVoiceHandle voice, bool paused);
    bool IsVoiceAlive(AudioVoiceHandle voice) const noexcept;

    bool SetVoiceGain(AudioVoiceHandle voice, float gain);
    bool SetVoicePitch(AudioVoiceHandle voice, float pitch);
    bool SetVoiceBus(AudioVoiceHandle voice, AudioBusId bus);
    bool SetVoiceSpatialBlend(AudioVoiceHandle voice, float spatialBlend);
    bool SetVoicePosition(AudioVoiceHandle voice, Vector3 position);
    bool SetVoiceOrientation(AudioVoiceHandle voice, Vector3 forward);
    bool SetVoiceVelocity(AudioVoiceHandle voice, Vector3 velocity);
    bool SetVoiceDistanceRange(AudioVoiceHandle voice, float minDistance,
                               float maxDistance);
    bool SetVoiceAttenuation(AudioVoiceHandle voice, AudioAttenuationModel model,
                              float nearGain, float farGain,
                              float exponent = 1.0f);
    bool SetVoiceDoppler(AudioVoiceHandle voice, float dopplerScale);
    bool SetVoiceCone(AudioVoiceHandle voice, float innerConeDegrees,
                      float outerConeDegrees, float outerConeGain);
    bool SetVoiceSpatialState(AudioVoiceHandle voice,
                              const AudioSourceState& source);

    void SetListener(const AudioListenerState& listener);
    void SetBusGain(AudioBusId bus, float gain);
    void SetBusMute(AudioBusId bus, bool mute);

    AudioStats Stats() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Concord::Audio

#endif // CONCORD_AUDIORUNTIME_H
