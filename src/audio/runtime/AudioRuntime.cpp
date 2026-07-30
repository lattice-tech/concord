#include "audio/runtime/AudioRuntime.h"

#include <SDL3/SDL.h>

#include "audio/runtime/detail/AudioClipRegistry.h"
#include "audio/runtime/detail/AudioDevice.h"
#include "audio/runtime/detail/AudioMixer.h"
#include "audio/runtime/detail/AudioMixerState.h"
#include "audio/runtime/detail/AudioValidation.h"
#include "audio/runtime/detail/AudioVoicePool.h"

#include <cmath>
#include <bit>
#include <mutex>
#include <string>
#include <unordered_map>
#include <memory>
#include <utility>

namespace Concord::Audio {

class AudioRuntime::Impl {
public:
    struct TransientClipBinding {
        AudioVoiceHandle voice{};
        AudioClipHandle clip{};
    };

    struct BuiltInCacheKey {
        AudioBuiltInSound sound = AudioBuiltInSound::Beep;
        std::int32_t sampleRate = 48000;

        bool operator==(const BuiltInCacheKey& other) const noexcept
        {
            return sound == other.sound && sampleRate == other.sampleRate;
        }
    };

    struct BuiltInCacheKeyHash {
        std::size_t operator()(const BuiltInCacheKey& key) const noexcept
        {
            return (static_cast<std::size_t>(key.sampleRate) << 8)
                ^ static_cast<std::size_t>(key.sound);
        }
    };

    /** Full synthesis description as a cache key; two equal keys render the same PCM. */
    struct SynthCacheKey {
        AudioSynthesisDesc desc{};

        SynthCacheKey() = default;
        explicit SynthCacheKey(const AudioSynthesisDesc& d) : desc(d) {}

        bool operator==(const SynthCacheKey& other) const noexcept
        {
            const AudioSynthesisDesc& a = desc;
            const AudioSynthesisDesc& b = other.desc;
            return a.sampleRate == b.sampleRate && a.channels == b.channels
                && a.durationSeconds == b.durationSeconds
                && a.frequencyHz == b.frequencyHz
                && a.endFrequencyHz == b.endFrequencyHz
                && a.amplitude == b.amplitude
                && a.vibratoHz == b.vibratoHz && a.vibratoDepth == b.vibratoDepth
                && a.tremoloHz == b.tremoloHz && a.tremoloDepth == b.tremoloDepth
                && a.noiseSeed == b.noiseSeed && a.waveform == b.waveform
                && a.envelope.attackSeconds == b.envelope.attackSeconds
                && a.envelope.decaySeconds == b.envelope.decaySeconds
                && a.envelope.sustainLevel == b.envelope.sustainLevel
                && a.envelope.releaseSeconds == b.envelope.releaseSeconds;
        }
    };

    struct SynthCacheKeyHash {
        std::size_t operator()(const SynthCacheKey& key) const noexcept
        {
            const AudioSynthesisDesc& d = key.desc;
            std::size_t h = static_cast<std::size_t>(d.sampleRate);
            h ^= (static_cast<std::size_t>(d.channels) << 8)
                ^ (static_cast<std::size_t>(d.noiseSeed) << 16)
                ^ (static_cast<std::size_t>(d.waveform) << 24);
            const auto mix = [&h](float value) {
                h ^= std::bit_cast<std::uint32_t>(value) + 0x9e3779b9u
                    + (h << 6) + (h >> 2);
            };
            mix(d.durationSeconds);
            mix(d.frequencyHz);
            mix(d.endFrequencyHz);
            mix(d.amplitude);
            mix(d.vibratoHz);
            mix(d.vibratoDepth);
            mix(d.tremoloHz);
            mix(d.tremoloDepth);
            mix(d.envelope.attackSeconds);
            mix(d.envelope.decaySeconds);
            mix(d.envelope.sustainLevel);
            mix(d.envelope.releaseSeconds);
            return h;
        }
    };

    std::unordered_map<std::string, AudioClipHandle> m_wavCache;
    std::unordered_map<SynthCacheKey, AudioClipHandle, SynthCacheKeyHash> m_synthCache;
    std::unordered_map<BuiltInCacheKey, AudioClipHandle, BuiltInCacheKeyHash> m_builtInCache;

    /**
     * Guards everything the mixing thread reads (voices, clips, listener, bus
     * state). Recursive because the public entry points funnel through each
     * other (PlayTransient -> Play) and Pump re-enters the device fill.
     */
    mutable std::recursive_mutex m_renderLock;

    bool Init(const AudioRuntimeConfig& config)
    {
        // Unlocked: Shutdown joins the mixing thread, and holding the render
        // lock across a join would deadlock against an in-flight fill.
        Shutdown();
        const std::lock_guard<std::recursive_mutex> lock(m_renderLock);
        if (config.device.sampleRate < 8000 || config.device.frameSize <= 0
            || config.device.bufferedFrames < config.device.frameSize
            || config.maxClips == 0 || config.maxVoices == 0
            || config.maxSpatialVoices > config.maxVoices
            || config.commandQueueCapacity == 0 || config.streamBufferFrames == 0) {
            return false;
        }
        m_config = config;
        m_initialized = true;
        m_stats = {};
        m_stats.initialized = true;
        m_listener = {};
        m_transientClips.clear();
        m_wavCache.clear();
        m_synthCache.clear();
        m_builtInCache.clear();
        m_clips.Reset(config.maxClips);
        m_voices.Reset(config.maxVoices);
        m_mixer.Reset(config.device.startMuted);
        if (!m_audioMixer.Init(config)
            || !m_device.Init(config, m_audioMixer, &m_listener, &m_clips,
                              &m_voices, &m_mixer, &m_stats, &m_renderLock)) {
            Shutdown();
            return false;
        }
        RefreshStats();
        return true;
    }

    void Shutdown() noexcept
    {
        m_device.Shutdown();
        m_audioMixer.Shutdown();
        m_initialized = false;
        m_config = {};
        m_listener = {};
        m_transientClips.clear();
        m_wavCache.clear();
        m_synthCache.clear();
        m_builtInCache.clear();
        m_clips.Reset(0);
        m_voices.Reset(0);
        m_mixer.Reset(false);
        m_stats = {};
    }

    bool IsInitialized() const noexcept
    {
        return m_initialized;
    }

    void Pump() noexcept
    {
        if (m_initialized) {
            m_device.Pump();
        }
    }

    AudioClipHandle CreateClipFromPcm(const AudioClipDesc& desc,
                                      std::span<const float> samples)
    {
        const std::lock_guard<std::recursive_mutex> lock(m_renderLock);
        CleanupTransientClips();
        return m_initialized ? m_clips.CreateFromPcm(desc, samples) : AudioClipHandle{};
    }

    void DestroyClip(AudioClipHandle clip)
    {
        const std::lock_guard<std::recursive_mutex> lock(m_renderLock);
        if (!m_initialized || !m_clips.Destroy(clip)) {
            return;
        }
        m_voices.StopVoicesUsingClip(clip);
        RefreshStats();
    }

    AudioVoiceHandle Play(AudioClipHandle clip, const AudioPlayParams& params, bool loop)
    {
        const std::lock_guard<std::recursive_mutex> lock(m_renderLock);
        CleanupTransientClips();
        if (!m_initialized) {
            return {};
        }
        const AudioVoiceHandle handle = m_voices.Play(clip, params, loop, m_clips,
                                                       m_stats.rejectedCommands);
        if (handle.IsValid()) {
            RefreshStats();
            return handle;
        }
        const AudioVoiceHandle stolen = m_voices.StealOrPlay(clip, params, loop, m_clips,
                                                             m_stats.rejectedCommands);
        RefreshStats();
        return stolen;
    }

    AudioVoiceHandle PlaySpatial(AudioClipHandle clip, const AudioSourceState& source,
                                 const AudioPlayParams& params, bool loop)
    {
        AudioPlayParams spatial = params;
        spatial.spatial = true;
        spatial.spatialBlend = source.spatialBlend;
        spatial.source = source;
        return Play(clip, spatial, loop);
    }

    AudioVoiceHandle PlayTransient(const AudioClipDesc& desc,
                                   std::span<const float> samples,
                                   const AudioPlayParams& params,
                                   bool loop, bool spatial,
                                   const AudioSourceState* source)
    {
        const std::lock_guard<std::recursive_mutex> lock(m_renderLock);
        CleanupTransientClips();
        const AudioClipHandle clip = CreateClipFromPcm(desc, samples);
        if (!clip.IsValid()) {
            return {};
        }
        AudioVoiceHandle voice = spatial && source != nullptr
            ? PlaySpatial(clip, *source, params, loop)
            : Play(clip, params, loop);
        if (!voice.IsValid()) {
            m_clips.Destroy(clip);
            return {};
        }
        m_transientClips.push_back({voice, clip});
        return voice;
    }

    void StopVoice(AudioVoiceHandle voice)
    {
        const std::lock_guard<std::recursive_mutex> lock(m_renderLock);
        if (!m_initialized || !m_voices.Stop(voice)) {
            return;
        }
        RefreshStats();
    }

    bool PauseVoice(AudioVoiceHandle voice, bool paused)
    {
        const std::lock_guard<std::recursive_mutex> lock(m_renderLock);
        return m_initialized && m_voices.Pause(voice, paused);
    }

    bool IsVoiceAlive(AudioVoiceHandle voice) const noexcept
    {
        // The mixing thread flips `active` when a one-shot finishes, so even
        // this liveness probe must agree on a snapshot.
        const std::lock_guard<std::recursive_mutex> lock(m_renderLock);
        return m_initialized && m_voices.IsAlive(voice);
    }

    void StopAllVoices(AudioBusId bus)
    {
        const std::lock_guard<std::recursive_mutex> lock(m_renderLock);
        if (!m_initialized) {
            return;
        }
        m_voices.StopAll(bus);
        RefreshStats();
    }

    bool SetVoiceGain(AudioVoiceHandle voice, float gain)
    {
        const std::lock_guard<std::recursive_mutex> lock(m_renderLock);
        return m_initialized && m_voices.SetGain(voice, gain);
    }

    bool SetVoicePitch(AudioVoiceHandle voice, float pitch)
    {
        const std::lock_guard<std::recursive_mutex> lock(m_renderLock);
        return m_initialized && m_voices.SetPitch(voice, pitch);
    }

    bool SetVoiceBus(AudioVoiceHandle voice, AudioBusId bus)
    {
        const std::lock_guard<std::recursive_mutex> lock(m_renderLock);
        return m_initialized && m_voices.SetBus(voice, bus);
    }

    bool SetVoiceSpatialBlend(AudioVoiceHandle voice, float spatialBlend)
    {
        const std::lock_guard<std::recursive_mutex> lock(m_renderLock);
        return m_initialized && m_voices.SetSpatialBlend(voice, spatialBlend);
    }

    bool SetVoicePosition(AudioVoiceHandle voice, Vector3 position)
    {
        const std::lock_guard<std::recursive_mutex> lock(m_renderLock);
        return UpdateVoiceSource(voice, [position](AudioSourceState& source) {
            source.position = position;
        });
    }

    bool SetVoiceOrientation(AudioVoiceHandle voice, Vector3 forward)
    {
        const std::lock_guard<std::recursive_mutex> lock(m_renderLock);
        return UpdateVoiceSource(voice, [forward](AudioSourceState& source) {
            source.forward = forward;
        });
    }

    bool SetVoiceVelocity(AudioVoiceHandle voice, Vector3 velocity)
    {
        const std::lock_guard<std::recursive_mutex> lock(m_renderLock);
        return UpdateVoiceSource(voice, [velocity](AudioSourceState& source) {
            source.velocity = velocity;
        });
    }

    bool SetVoiceDistanceRange(AudioVoiceHandle voice, float minDistance,
                               float maxDistance)
    {
        const std::lock_guard<std::recursive_mutex> lock(m_renderLock);
        if (!std::isfinite(minDistance) || !std::isfinite(maxDistance)
            || minDistance < 0.0f || maxDistance < minDistance) {
            return false;
        }
        return UpdateVoiceSource(voice, [minDistance, maxDistance](AudioSourceState& source) {
            source.minDistance = minDistance;
            source.maxDistance = maxDistance;
        });
    }

    bool SetVoiceAttenuation(AudioVoiceHandle voice, AudioAttenuationModel model,
                             float nearGain, float farGain, float exponent)
    {
        const std::lock_guard<std::recursive_mutex> lock(m_renderLock);
        if (!std::isfinite(nearGain) || !std::isfinite(farGain)
            || !std::isfinite(exponent)) {
            return false;
        }
        return UpdateVoiceSource(voice, [model, nearGain, farGain, exponent](AudioSourceState& source) {
            source.attenuation = model;
            source.nearGain = nearGain;
            source.farGain = farGain;
            source.attenuationExponent = exponent;
        });
    }

    bool SetVoiceDoppler(AudioVoiceHandle voice, float dopplerScale)
    {
        const std::lock_guard<std::recursive_mutex> lock(m_renderLock);
        if (!std::isfinite(dopplerScale)) {
            return false;
        }
        return UpdateVoiceSource(voice, [dopplerScale](AudioSourceState& source) {
            source.dopplerScale = dopplerScale;
        });
    }

    bool SetVoiceCone(AudioVoiceHandle voice, float innerConeDegrees,
                      float outerConeDegrees, float outerConeGain)
    {
        const std::lock_guard<std::recursive_mutex> lock(m_renderLock);
        if (!std::isfinite(innerConeDegrees) || !std::isfinite(outerConeDegrees)
            || !std::isfinite(outerConeGain) || innerConeDegrees < 0.0f
            || outerConeDegrees < innerConeDegrees) {
            return false;
        }
        return UpdateVoiceSource(voice, [innerConeDegrees, outerConeDegrees, outerConeGain](AudioSourceState& source) {
            source.innerConeDegrees = innerConeDegrees;
            source.outerConeDegrees = outerConeDegrees;
            source.outerConeGain = outerConeGain;
        });
    }

    bool SetVoiceSpatialState(AudioVoiceHandle voice, const AudioSourceState& source)
    {
        const std::lock_guard<std::recursive_mutex> lock(m_renderLock);
        return m_initialized && m_voices.SetSpatialState(voice, source);
    }

    void SetListener(const AudioListenerState& listener)
    {
        const std::lock_guard<std::recursive_mutex> lock(m_renderLock);
        if (!m_initialized || !Detail::IsFinite(listener)) {
            return;
        }
        m_listener = listener;
    }

    void SetBusGain(AudioBusId bus, float gain)
    {
        const std::lock_guard<std::recursive_mutex> lock(m_renderLock);
        if (!m_initialized || !std::isfinite(gain) || gain < 0.0f) {
            return;
        }
        m_mixer.SetBusGain(bus, gain);
    }

    void SetBusMute(AudioBusId bus, bool mute)
    {
        const std::lock_guard<std::recursive_mutex> lock(m_renderLock);
        if (!m_initialized) {
            return;
        }
        m_mixer.SetBusMute(bus, mute);
    }

    AudioStats Stats() const noexcept
    {
        return m_stats;
    }

private:
    template <typename Mutator>
    bool UpdateVoiceSource(AudioVoiceHandle voice, Mutator mutator)
    {
        if (!m_initialized) {
            return false;
        }
        AudioSourceState current;
        if (!m_voices.QuerySource(voice, current)) {
            return false;
        }
        mutator(current);
        return m_voices.SetSpatialState(voice, current);
    }

    void CleanupTransientClips()
    {
        for (std::size_t index = 0; index < m_transientClips.size();) {
            const TransientClipBinding binding = m_transientClips[index];
            if (m_voices.IsAlive(binding.voice)) {
                ++index;
                continue;
            }
            m_clips.Destroy(binding.clip);
            m_transientClips.erase(m_transientClips.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }

    void RefreshStats() noexcept
    {
        m_stats.initialized = m_initialized;
        m_stats.activeVoices = m_voices.ActiveVoiceCount();
        m_stats.activeSpatialVoices = m_voices.ActiveSpatialVoiceCount();
    }

    AudioRuntimeConfig m_config{};
    AudioListenerState m_listener{};
    Detail::AudioClipRegistry m_clips;
    Detail::AudioVoicePool m_voices;
    Detail::AudioMixerState m_mixer;
    Detail::AudioMixer m_audioMixer;
    Detail::AudioDevice m_device;
    std::vector<TransientClipBinding> m_transientClips;
    AudioStats m_stats{};
    bool m_initialized = false;
};

AudioRuntime::AudioRuntime() = default;
AudioRuntime::~AudioRuntime() = default;
AudioRuntime::AudioRuntime(AudioRuntime&& other) noexcept = default;
AudioRuntime& AudioRuntime::operator=(AudioRuntime&& other) noexcept = default;

bool AudioRuntime::Init(const AudioRuntimeConfig& config)
{
    if (!m_impl) {
        m_impl = std::make_unique<Impl>();
    }
    return m_impl->Init(config);
}

void AudioRuntime::Shutdown() noexcept
{
    if (m_impl) {
        m_impl->Shutdown();
    }
}

bool AudioRuntime::IsInitialized() const noexcept
{
    return m_impl && m_impl->IsInitialized();
}

void AudioRuntime::Pump() noexcept
{
    if (m_impl) {
        m_impl->Pump();
    }
}

AudioClipHandle AudioRuntime::CreateClipFromPcm(const AudioClipDesc& desc,
                                                std::span<const float> samples)
{
    return m_impl ? m_impl->CreateClipFromPcm(desc, samples) : AudioClipHandle{};
}

AudioClipHandle AudioRuntime::LoadWavClip(const std::string& path)
{
    if (m_impl && m_impl->IsInitialized()) {
        const auto it = m_impl->m_wavCache.find(path);
        if (it != m_impl->m_wavCache.end()) {
            return it->second;
        }
    }
    SDL_AudioSpec srcSpec{};
    Uint8* audioBuffer = nullptr;
    Uint32 audioBytes = 0;
    if (!SDL_LoadWAV(path.c_str(), &srcSpec, &audioBuffer, &audioBytes)) {
        return {};
    }

    SDL_AudioSpec dstSpec{};
    dstSpec.format = SDL_AUDIO_F32;
    dstSpec.channels = srcSpec.channels;
    dstSpec.freq = srcSpec.freq;

    Uint8* converted = nullptr;
    int convertedBytes = 0;
    AudioClipHandle handle{};
    if (SDL_ConvertAudioSamples(&srcSpec, audioBuffer, static_cast<int>(audioBytes),
                                &dstSpec, &converted, &convertedBytes)) {
        const auto* floats = reinterpret_cast<const float*>(converted);
        const std::size_t sampleCount = static_cast<std::size_t>(convertedBytes) / sizeof(float);
        AudioClipDesc desc;
        desc.sampleRate = dstSpec.freq;
        desc.channels = dstSpec.channels;
        desc.frameCount = dstSpec.channels == 0 ? 0
            : static_cast<std::uint32_t>(sampleCount / dstSpec.channels);
        desc.streaming = false;
        desc.spatializable = dstSpec.channels == 1;
        handle = CreateClipFromPcm(desc, std::span<const float>(floats, sampleCount));
    }

    if (converted != nullptr) {
        SDL_free(converted);
    }
    if (audioBuffer != nullptr) {
        SDL_free(audioBuffer);
    }
    if (handle.IsValid() && m_impl && m_impl->IsInitialized()) {
        m_impl->m_wavCache[path] = handle;
    }
    return handle;
}

AudioClipHandle AudioRuntime::CreateSynthClip(const AudioSynthesisDesc& desc)
{
    if (m_impl && m_impl->IsInitialized()) {
        const auto key = AudioRuntime::Impl::SynthCacheKey(desc);
        const auto it = m_impl->m_synthCache.find(key);
        if (it != m_impl->m_synthCache.end()) {
            return it->second;
        }
    }
    const AudioSynthBuffer buffer = AudioSynth::Generate(desc);
    AudioClipDesc clipDesc;
    clipDesc.sampleRate = buffer.sampleRate;
    clipDesc.channels = buffer.channels;
    clipDesc.frameCount = buffer.channels == 0 ? 0
        : static_cast<std::uint32_t>(buffer.samples.size() / buffer.channels);
    clipDesc.streaming = false;
    clipDesc.spatializable = buffer.channels == 1;
    const AudioClipHandle handle = CreateClipFromPcm(clipDesc, buffer.samples);
    if (handle.IsValid() && m_impl && m_impl->IsInitialized()) {
        m_impl->m_synthCache[AudioRuntime::Impl::SynthCacheKey(desc)] = handle;
    }
    return handle;
}

AudioClipHandle AudioRuntime::CreateBuiltInClip(AudioBuiltInSound sound,
                                                std::int32_t sampleRate)
{
    if (m_impl && m_impl->IsInitialized()) {
        const Impl::BuiltInCacheKey key{sound, sampleRate};
        const auto it = m_impl->m_builtInCache.find(key);
        if (it != m_impl->m_builtInCache.end()) {
            return it->second;
        }
    }
    const AudioSynthBuffer buffer = AudioSynth::GenerateBuiltIn(sound, sampleRate);
    AudioClipDesc clipDesc;
    clipDesc.sampleRate = buffer.sampleRate;
    clipDesc.channels = buffer.channels;
    clipDesc.frameCount = buffer.channels == 0 ? 0
        : static_cast<std::uint32_t>(buffer.samples.size() / buffer.channels);
    clipDesc.streaming = false;
    clipDesc.spatializable = buffer.channels == 1;
    const AudioClipHandle handle = CreateClipFromPcm(clipDesc, buffer.samples);
    if (handle.IsValid() && m_impl && m_impl->IsInitialized()) {
        m_impl->m_builtInCache[{sound, sampleRate}] = handle;
    }
    return handle;
}

void AudioRuntime::DestroyClip(AudioClipHandle clip)
{
    if (m_impl) {
        m_impl->DestroyClip(clip);
    }
}

AudioVoiceHandle AudioRuntime::PlayOneShotPcm(const AudioClipDesc& desc,
                                              std::span<const float> samples,
                                              const AudioPlayParams& params)
{
    return m_impl ? m_impl->PlayTransient(desc, samples, params, false, false, nullptr)
                  : AudioVoiceHandle{};
}

AudioVoiceHandle AudioRuntime::PlayLoopPcm(const AudioClipDesc& desc,
                                           std::span<const float> samples,
                                           const AudioPlayParams& params)
{
    return m_impl ? m_impl->PlayTransient(desc, samples, params, true, false, nullptr)
                  : AudioVoiceHandle{};
}

AudioVoiceHandle AudioRuntime::PlaySpatialOneShotPcm(const AudioClipDesc& desc,
                                                     std::span<const float> samples,
                                                     const AudioSourceState& source,
                                                     const AudioPlayParams& params)
{
    return m_impl ? m_impl->PlayTransient(desc, samples, params, false, true, &source)
                  : AudioVoiceHandle{};
}

AudioVoiceHandle AudioRuntime::PlaySpatialLoopPcm(const AudioClipDesc& desc,
                                                   std::span<const float> samples,
                                                   const AudioSourceState& source,
                                                   const AudioPlayParams& params)
{
    return m_impl ? m_impl->PlayTransient(desc, samples, params, true, true, &source)
                  : AudioVoiceHandle{};
}

AudioVoiceHandle AudioRuntime::PlaySynthOneShot(const AudioSynthesisDesc& desc,
                                                const AudioPlayParams& params)
{
    const AudioSynthBuffer buffer = AudioSynth::Generate(desc);
    AudioClipDesc clipDesc;
    clipDesc.sampleRate = buffer.sampleRate;
    clipDesc.channels = buffer.channels;
    clipDesc.frameCount = buffer.channels == 0 ? 0
        : static_cast<std::uint32_t>(buffer.samples.size() / buffer.channels);
    clipDesc.streaming = false;
    clipDesc.spatializable = buffer.channels == 1;
    return PlayOneShotPcm(clipDesc, buffer.samples, params);
}

AudioVoiceHandle AudioRuntime::PlaySynthLoop(const AudioSynthesisDesc& desc,
                                             const AudioPlayParams& params)
{
    const AudioSynthBuffer buffer = AudioSynth::Generate(desc);
    AudioClipDesc clipDesc;
    clipDesc.sampleRate = buffer.sampleRate;
    clipDesc.channels = buffer.channels;
    clipDesc.frameCount = buffer.channels == 0 ? 0
        : static_cast<std::uint32_t>(buffer.samples.size() / buffer.channels);
    clipDesc.streaming = false;
    clipDesc.spatializable = buffer.channels == 1;
    return PlayLoopPcm(clipDesc, buffer.samples, params);
}

AudioVoiceHandle AudioRuntime::PlaySynthAt(const AudioSynthesisDesc& desc, const Vector3& position,
                                           const AudioPlayParams& params)
{
    const AudioSynthBuffer buffer = AudioSynth::Generate(desc);
    AudioClipDesc clipDesc;
    clipDesc.sampleRate = buffer.sampleRate;
    clipDesc.channels = buffer.channels;
    clipDesc.frameCount = buffer.channels == 0 ? 0
        : static_cast<std::uint32_t>(buffer.samples.size() / buffer.channels);
    clipDesc.streaming = false;
    clipDesc.spatializable = buffer.channels == 1;
    AudioSourceState source{};
    source.position = position;
    source.spatialBlend = params.spatialBlend;
    return PlaySpatialOneShotPcm(clipDesc, buffer.samples, source, params);
}

AudioVoiceHandle AudioRuntime::PlaySynthLoopAt(const AudioSynthesisDesc& desc, const Vector3& position,
                                               const AudioPlayParams& params)
{
    const AudioSynthBuffer buffer = AudioSynth::Generate(desc);
    AudioClipDesc clipDesc;
    clipDesc.sampleRate = buffer.sampleRate;
    clipDesc.channels = buffer.channels;
    clipDesc.frameCount = buffer.channels == 0 ? 0
        : static_cast<std::uint32_t>(buffer.samples.size() / buffer.channels);
    clipDesc.streaming = false;
    clipDesc.spatializable = buffer.channels == 1;
    AudioSourceState source{};
    source.position = position;
    source.spatialBlend = params.spatialBlend;
    return PlaySpatialLoopPcm(clipDesc, buffer.samples, source, params);
}

AudioVoiceHandle AudioRuntime::PlayBuiltInOneShot(AudioBuiltInSound sound,
                                                  const AudioPlayParams& params)
{
    return PlayOneShot(CreateBuiltInClip(sound), params);
}

AudioVoiceHandle AudioRuntime::PlayBuiltInLoop(AudioBuiltInSound sound,
                                               const AudioPlayParams& params)
{
    return PlayLoop(CreateBuiltInClip(sound), params);
}

AudioVoiceHandle AudioRuntime::PlayBuiltInAt(AudioBuiltInSound sound, const Vector3& position,
                                             const AudioPlayParams& params)
{
    AudioSourceState source{};
    source.position = position;
    source.spatialBlend = params.spatialBlend;
    return PlaySpatialOneShot(CreateBuiltInClip(sound), source, params);
}

AudioVoiceHandle AudioRuntime::PlayBuiltInLoopAt(AudioBuiltInSound sound, const Vector3& position,
                                                 const AudioPlayParams& params)
{
    AudioSourceState source{};
    source.position = position;
    source.spatialBlend = params.spatialBlend;
    return PlaySpatialLoop(CreateBuiltInClip(sound), source, params);
}

AudioVoiceHandle AudioRuntime::PlayOneShot(AudioClipHandle clip,
                                           const AudioPlayParams& params)
{
    return m_impl ? m_impl->Play(clip, params, false) : AudioVoiceHandle{};
}

AudioVoiceHandle AudioRuntime::PlayLoop(AudioClipHandle clip,
                                        const AudioPlayParams& params)
{
    return m_impl ? m_impl->Play(clip, params, true) : AudioVoiceHandle{};
}

AudioVoiceHandle AudioRuntime::PlaySpatialOneShot(AudioClipHandle clip,
                                                  const AudioSourceState& source,
                                                  const AudioPlayParams& params)
{
    return m_impl ? m_impl->PlaySpatial(clip, source, params, false) : AudioVoiceHandle{};
}

AudioVoiceHandle AudioRuntime::PlaySpatialLoop(AudioClipHandle clip,
                                               const AudioSourceState& source,
                                               const AudioPlayParams& params)
{
    return m_impl ? m_impl->PlaySpatial(clip, source, params, true) : AudioVoiceHandle{};
}

AudioVoiceHandle AudioRuntime::PlayAt(AudioClipHandle clip, const Vector3& position,
                                      const AudioPlayParams& params)
{
    AudioSourceState source{};
    source.position = position;
    source.spatialBlend = params.spatialBlend;
    return m_impl ? m_impl->PlaySpatial(clip, source, params, false) : AudioVoiceHandle{};
}

AudioVoiceHandle AudioRuntime::PlayLoopAt(AudioClipHandle clip, const Vector3& position,
                                          const AudioPlayParams& params)
{
    AudioSourceState source{};
    source.position = position;
    source.spatialBlend = params.spatialBlend;
    return m_impl ? m_impl->PlaySpatial(clip, source, params, true) : AudioVoiceHandle{};
}

void AudioRuntime::StopVoice(AudioVoiceHandle voice)
{
    if (m_impl) {
        m_impl->StopVoice(voice);
    }
}

bool AudioRuntime::PauseVoice(AudioVoiceHandle voice, bool paused)
{
    return m_impl && m_impl->PauseVoice(voice, paused);
}

bool AudioRuntime::IsVoiceAlive(AudioVoiceHandle voice) const noexcept
{
    return m_impl && m_impl->IsVoiceAlive(voice);
}

void AudioRuntime::StopAllVoices(AudioBusId bus)
{
    if (m_impl) {
        m_impl->StopAllVoices(bus);
    }
}

bool AudioRuntime::SetVoiceGain(AudioVoiceHandle voice, float gain)
{
    return m_impl && m_impl->SetVoiceGain(voice, gain);
}

bool AudioRuntime::SetVoicePitch(AudioVoiceHandle voice, float pitch)
{
    return m_impl && m_impl->SetVoicePitch(voice, pitch);
}

bool AudioRuntime::SetVoiceBus(AudioVoiceHandle voice, AudioBusId bus)
{
    return m_impl && m_impl->SetVoiceBus(voice, bus);
}

bool AudioRuntime::SetVoiceSpatialBlend(AudioVoiceHandle voice, float spatialBlend)
{
    return m_impl && m_impl->SetVoiceSpatialBlend(voice, spatialBlend);
}

bool AudioRuntime::SetVoicePosition(AudioVoiceHandle voice, Vector3 position)
{
    return m_impl && m_impl->SetVoicePosition(voice, position);
}

bool AudioRuntime::SetVoiceOrientation(AudioVoiceHandle voice, Vector3 forward)
{
    return m_impl && m_impl->SetVoiceOrientation(voice, forward);
}

bool AudioRuntime::SetVoiceVelocity(AudioVoiceHandle voice, Vector3 velocity)
{
    return m_impl && m_impl->SetVoiceVelocity(voice, velocity);
}

bool AudioRuntime::SetVoiceDistanceRange(AudioVoiceHandle voice, float minDistance,
                                         float maxDistance)
{
    return m_impl && m_impl->SetVoiceDistanceRange(voice, minDistance, maxDistance);
}

bool AudioRuntime::SetVoiceAttenuation(AudioVoiceHandle voice,
                                       AudioAttenuationModel model,
                                       float nearGain, float farGain,
                                       float exponent)
{
    return m_impl && m_impl->SetVoiceAttenuation(voice, model, nearGain, farGain, exponent);
}

bool AudioRuntime::SetVoiceDoppler(AudioVoiceHandle voice, float dopplerScale)
{
    return m_impl && m_impl->SetVoiceDoppler(voice, dopplerScale);
}

bool AudioRuntime::SetVoiceCone(AudioVoiceHandle voice, float innerConeDegrees,
                                float outerConeDegrees, float outerConeGain)
{
    return m_impl && m_impl->SetVoiceCone(voice, innerConeDegrees,
                                          outerConeDegrees, outerConeGain);
}

bool AudioRuntime::SetVoiceSpatialState(AudioVoiceHandle voice,
                                        const AudioSourceState& source)
{
    return m_impl && m_impl->SetVoiceSpatialState(voice, source);
}

void AudioRuntime::SetListener(const AudioListenerState& listener)
{
    if (m_impl) {
        m_impl->SetListener(listener);
    }
}

void AudioRuntime::SetBusGain(AudioBusId bus, float gain)
{
    if (m_impl) {
        m_impl->SetBusGain(bus, gain);
    }
}

void AudioRuntime::SetBusMute(AudioBusId bus, bool mute)
{
    if (m_impl) {
        m_impl->SetBusMute(bus, mute);
    }
}

AudioStats AudioRuntime::Stats() const noexcept
{
    return m_impl ? m_impl->Stats() : AudioStats{};
}

} // namespace Concord::Audio
