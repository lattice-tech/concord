#include "audio/runtime/detail/AudioMixer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Concord::Audio::Detail {

namespace {

constexpr float kSpeedOfSound = 343.0f;

float Dot(const Vector3& lhs, const Vector3& rhs) noexcept
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

Vector3 NormalizeOrDefault(const Vector3& value, const Vector3& fallback) noexcept
{
    const float lengthSquared = Dot(value, value);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12f) {
        return fallback;
    }
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    return {value.x * inverseLength, value.y * inverseLength, value.z * inverseLength};
}

float DistanceSquared(const Vector3& lhs, const Vector3& rhs) noexcept
{
    const float dx = lhs.x - rhs.x;
    const float dy = lhs.y - rhs.y;
    const float dz = lhs.z - rhs.z;
    return dx * dx + dy * dy + dz * dz;
}

} // namespace

bool AudioMixer::Init(const AudioRuntimeConfig& config)
{
    Shutdown();
    m_config = config;
    m_spatializers.resize(config.maxSpatialVoices);
    SpatialAudioConfig spatialConfig{};
    spatialConfig.sampleRate = config.device.sampleRate;
    spatialConfig.frameSize = config.device.frameSize;
    spatialConfig.validation = config.device.validation;
    for (SteamAudioSpatializer& spatializer : m_spatializers) {
        if (!spatializer.Init(spatialConfig)) {
            Shutdown();
            return false;
        }
    }
    m_monoScratch.resize(static_cast<std::size_t>(config.device.frameSize));
    m_stereoScratch.resize(static_cast<std::size_t>(config.device.frameSize) * 2u);
    m_masterScratch.resize(static_cast<std::size_t>(config.device.frameSize) * 2u);
    m_musicScratch.resize(static_cast<std::size_t>(config.device.frameSize) * 2u);
    m_sfxScratch.resize(static_cast<std::size_t>(config.device.frameSize) * 2u);
    m_uiScratch.resize(static_cast<std::size_t>(config.device.frameSize) * 2u);
    m_dialogueScratch.resize(static_cast<std::size_t>(config.device.frameSize) * 2u);
    m_initialized = true;
    return true;
}

void AudioMixer::Shutdown() noexcept
{
    for (SteamAudioSpatializer& spatializer : m_spatializers) {
        spatializer.Shutdown();
    }
    m_spatializers.clear();
    m_monoScratch.clear();
    m_stereoScratch.clear();
    m_masterScratch.clear();
    m_musicScratch.clear();
    m_sfxScratch.clear();
    m_uiScratch.clear();
    m_dialogueScratch.clear();
    m_activeVoices.clear();
    m_config = {};
    m_initialized = false;
}

void AudioMixer::Render(float* interleavedStereo, std::uint32_t frames,
                        const AudioListenerState& listener,
                        const AudioClipRegistry& clips, AudioVoicePool& voices,
                        const AudioMixerState& buses, AudioStats& stats)
{
    if (!m_initialized || interleavedStereo == nullptr || frames == 0) {
        return;
    }
    std::memset(interleavedStereo, 0, static_cast<std::size_t>(frames) * 2u * sizeof(float));
    std::memset(m_masterScratch.data(), 0, static_cast<std::size_t>(frames) * 2u * sizeof(float));
    std::memset(m_musicScratch.data(), 0, static_cast<std::size_t>(frames) * 2u * sizeof(float));
    std::memset(m_sfxScratch.data(), 0, static_cast<std::size_t>(frames) * 2u * sizeof(float));
    std::memset(m_uiScratch.data(), 0, static_cast<std::size_t>(frames) * 2u * sizeof(float));
    std::memset(m_dialogueScratch.data(), 0, static_cast<std::size_t>(frames) * 2u * sizeof(float));
    if (buses.BusMuted(AudioBusId::Master) || buses.BusGain(AudioBusId::Master) <= 0.0f) {
        stats.peakMaster = 0.0f;
        return;
    }

    voices.CollectActive(m_activeVoices);
    std::stable_sort(m_activeVoices.begin(), m_activeVoices.end(),
                     [&listener](const ActiveVoiceView& lhs, const ActiveVoiceView& rhs) {
                         const bool lhsSpatial = lhs.params.spatial;
                         const bool rhsSpatial = rhs.params.spatial;
                         if (lhsSpatial != rhsSpatial) {
                             return lhsSpatial && !rhsSpatial;
                         }
                         const float lhsDistance = DistanceSquared(lhs.params.source.position,
                                                                   listener.position);
                         const float rhsDistance = DistanceSquared(rhs.params.source.position,
                                                                   listener.position);
                         if (lhsDistance != rhsDistance) {
                             return lhsDistance < rhsDistance;
                         }
                         return lhs.params.priority > rhs.params.priority;
                     });
    std::uint32_t spatialIndex = 0;
    float peak = 0.0f;
    for (const ActiveVoiceView& voice : m_activeVoices) {
        const AudioClipDesc* desc = clips.Describe(voice.clip);
        const float* samples = clips.Samples(voice.clip);
        if (desc == nullptr || samples == nullptr || desc->frameCount == 0) {
            continue;
        }
        if (buses.BusMuted(voice.params.bus) || buses.BusGain(voice.params.bus) <= 0.0f) {
            voices.Advance(voice.handle, frames, desc->frameCount);
            continue;
        }

        const float gain = voice.params.gain * buses.BusGain(AudioBusId::Master)
            * buses.BusGain(voice.params.bus);
        if (gain <= 0.0f) {
            voices.Advance(voice.handle, frames, desc->frameCount);
            continue;
        }

        float* busBuffer = m_sfxScratch.data();
        switch (voice.params.bus) {
        case AudioBusId::Master:
            busBuffer = m_masterScratch.data();
            break;
        case AudioBusId::Music:
            busBuffer = m_musicScratch.data();
            break;
        case AudioBusId::Sfx:
            busBuffer = m_sfxScratch.data();
            break;
        case AudioBusId::Ui:
            busBuffer = m_uiScratch.data();
            break;
        case AudioBusId::Dialogue:
            busBuffer = m_dialogueScratch.data();
            break;
        }

        if (desc->channels == 1) {
            std::fill(m_monoScratch.begin(), m_monoScratch.end(), 0.0f);
            const float pitch = EffectivePitch(voice, listener);
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                const std::uint64_t sampleFrame = voice.cursorFrame
                    + static_cast<std::uint64_t>(static_cast<float>(frame) * pitch);
                if (!voice.params.loop && sampleFrame >= desc->frameCount) {
                    break;
                }
                const std::uint32_t clipFrame = voice.params.loop
                    ? static_cast<std::uint32_t>(sampleFrame % desc->frameCount)
                    : static_cast<std::uint32_t>(sampleFrame);
                m_monoScratch[frame] = samples[clipFrame];
            }
            if (voice.params.spatial && spatialIndex < m_spatializers.size()) {
                SpatialAudioListener spatialListener{};
                spatialListener.position = listener.position;
                spatialListener.right = listener.right;
                spatialListener.up = listener.up;
                spatialListener.forward = listener.forward;
                SpatialAudioSource spatialSource{};
                spatialSource.position = voice.params.source.position;
                spatialSource.gain = voice.params.source.gain * DistanceGain(voice.params.source, listener);
                spatialSource.spatialBlend = voice.params.spatialBlend;
                if (m_spatializers[spatialIndex].Process(m_monoScratch, m_stereoScratch,
                                                         spatialListener, spatialSource)) {
                    AccumulateStereo(m_stereoScratch.data(), frames, gain, busBuffer);
                }
                ++spatialIndex;
            } else {
                for (std::uint32_t frame = 0; frame < frames; ++frame) {
                    const float sample = m_monoScratch[frame] * gain;
                    busBuffer[frame * 2u] += sample;
                    busBuffer[frame * 2u + 1u] += sample;
                }
            }
        } else {
            const float pitch = EffectivePitch(voice, listener);
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                const std::uint64_t sampleFrame = voice.cursorFrame
                    + static_cast<std::uint64_t>(static_cast<float>(frame) * pitch);
                if (!voice.params.loop && sampleFrame >= desc->frameCount) {
                    break;
                }
                const std::uint32_t clipFrame = voice.params.loop
                    ? static_cast<std::uint32_t>(sampleFrame % desc->frameCount)
                    : static_cast<std::uint32_t>(sampleFrame);
                busBuffer[frame * 2u] += samples[clipFrame * 2u] * gain;
                busBuffer[frame * 2u + 1u] += samples[clipFrame * 2u + 1u] * gain;
            }
        }
        voices.Advance(voice.handle, frames, desc->frameCount);
    }

    MixBusToMaster(m_musicScratch.data(), frames, 1.0f, m_masterScratch.data());
    MixBusToMaster(m_sfxScratch.data(), frames, 1.0f, m_masterScratch.data());
    MixBusToMaster(m_uiScratch.data(), frames, 1.0f, m_masterScratch.data());
    MixBusToMaster(m_dialogueScratch.data(), frames, 1.0f, m_masterScratch.data());
    MixBusToMaster(m_masterScratch.data(), frames, 1.0f, interleavedStereo);

    stats.peakMusic = PeakLevel(m_musicScratch.data(), frames);
    stats.peakSfx = PeakLevel(m_sfxScratch.data(), frames);
    stats.peakUi = PeakLevel(m_uiScratch.data(), frames);
    stats.peakDialogue = PeakLevel(m_dialogueScratch.data(), frames);

    for (std::uint32_t frame = 0; frame < frames * 2u; ++frame) {
        interleavedStereo[frame] = SoftLimit(interleavedStereo[frame]);
        peak = std::max(peak, std::abs(interleavedStereo[frame]));
    }
    stats.peakMaster = peak;
}

float AudioMixer::EffectivePitch(const ActiveVoiceView& voice,
                                 const AudioListenerState& listener) const noexcept
{
    float pitch = std::max(voice.params.pitch, 0.001f);
    const AudioSourceState& source = voice.params.source;
    if (source.dopplerScale == 0.0f) {
        return pitch;
    }
    const Vector3 offset{
        source.position.x - listener.position.x,
        source.position.y - listener.position.y,
        source.position.z - listener.position.z,
    };
    const Vector3 direction = NormalizeOrDefault(offset, {0.0f, 0.0f, 1.0f});
    const float listenerVelocity = Dot(listener.velocity, direction);
    const float sourceVelocity = Dot(source.velocity, direction);
    const float numerator = kSpeedOfSound - source.dopplerScale * listenerVelocity;
    const float denominator = kSpeedOfSound - source.dopplerScale * sourceVelocity;
    if (numerator <= 1.0f || denominator <= 1.0f || !std::isfinite(numerator)
        || !std::isfinite(denominator)) {
        return pitch;
    }
    const float doppler = std::clamp(numerator / denominator, 0.5f, 2.0f);
    return pitch * doppler;
}

float AudioMixer::DistanceGain(const AudioSourceState& source,
                               const AudioListenerState& listener) const noexcept
{
    const float dx = source.position.x - listener.position.x;
    const float dy = source.position.y - listener.position.y;
    const float dz = source.position.z - listener.position.z;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!std::isfinite(distance)) {
        return 0.0f;
    }
    if (source.attenuation == AudioAttenuationModel::None) {
        return source.nearGain;
    }
    float distanceGain = source.farGain;
    if (distance <= source.minDistance) {
        distanceGain = source.nearGain;
    } else if (distance < source.maxDistance && source.maxDistance > source.minDistance) {
        const float t = (distance - source.minDistance) / (source.maxDistance - source.minDistance);
        if (source.attenuation == AudioAttenuationModel::Inverse) {
            const float exponent = std::max(source.attenuationExponent, 0.001f);
            distanceGain = source.nearGain / std::pow(1.0f + t, exponent);
        } else {
            const float shaped = std::pow(std::clamp(t, 0.0f, 1.0f),
                                          std::max(source.attenuationExponent, 0.001f));
            distanceGain = source.nearGain + (source.farGain - source.nearGain) * shaped;
        }
    }

    float coneGain = 1.0f;
    if (source.outerConeDegrees < 360.0f) {
        const Vector3 forward = NormalizeOrDefault(source.forward, {0.0f, 0.0f, 1.0f});
        const Vector3 toListener = NormalizeOrDefault(
            {listener.position.x - source.position.x,
             listener.position.y - source.position.y,
             listener.position.z - source.position.z},
            {0.0f, 0.0f, 1.0f});
        const float cosine = std::clamp(Dot(forward, toListener), -1.0f, 1.0f);
        const float angleDegrees = std::acos(cosine) * (180.0f / 3.14159265358979323846f);
        if (angleDegrees >= source.outerConeDegrees * 0.5f) {
            coneGain = source.outerConeGain;
        } else if (angleDegrees > source.innerConeDegrees * 0.5f
                   && source.outerConeDegrees > source.innerConeDegrees) {
            const float inner = source.innerConeDegrees * 0.5f;
            const float outer = source.outerConeDegrees * 0.5f;
            const float t = (angleDegrees - inner) / (outer - inner);
            coneGain = 1.0f + (source.outerConeGain - 1.0f) * std::clamp(t, 0.0f, 1.0f);
        }
    }
    return std::max(0.0f, distanceGain * coneGain);
}

float AudioMixer::PeakLevel(const float* bus, std::uint32_t frames) const noexcept
{
    float peak = 0.0f;
    for (std::uint32_t frame = 0; frame < frames * 2u; ++frame) {
        peak = std::max(peak, std::abs(bus[frame]));
    }
    return peak;
}

float AudioMixer::SoftLimit(float sample) const noexcept
{
    if (!std::isfinite(sample)) {
        return 0.0f;
    }
    if (std::abs(sample) <= 0.95f) {
        return sample;
    }
    const float sign = sample < 0.0f ? -1.0f : 1.0f;
    const float excess = std::abs(sample) - 0.95f;
    return sign * (0.95f + std::tanh(excess) * 0.05f);
}

void AudioMixer::AccumulateStereo(const float* stereo, std::uint32_t frames,
                                  float gain, float* out) noexcept
{
    for (std::uint32_t frame = 0; frame < frames * 2u; ++frame) {
        out[frame] += stereo[frame] * gain;
    }
}

void AudioMixer::MixBusToMaster(const float* bus, std::uint32_t frames,
                                float gain, float* out) noexcept
{
    for (std::uint32_t frame = 0; frame < frames * 2u; ++frame) {
        out[frame] += bus[frame] * gain;
    }
}

} // namespace Concord::Audio::Detail
