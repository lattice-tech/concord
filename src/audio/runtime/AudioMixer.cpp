#include "audio/runtime/detail/AudioMixer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Concord::Audio::Detail {

namespace {

constexpr float kSpeedOfSound = 343.0f;
constexpr float kPi = 3.14159265358979323846f;

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

std::uint64_t VoiceKey(const AudioVoiceHandle& handle) noexcept
{
    return (static_cast<std::uint64_t>(handle.Generation()) << 32u)
        | handle.Slot();
}

constexpr std::uint32_t kNoSpatialSlot = 0xFFFFFFFFu;

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
    if (!m_busRack.Init(config)) {
        Shutdown();
        return false;
    }
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
    m_activeVoices.clear();
    m_spatialAssignments.clear();
    m_occlusionStates.clear();
    m_spatialSlotUsed.clear();
    m_busRack.Shutdown();
    m_config = {};
    m_initialized = false;
}

void AudioMixer::Render(float* interleavedStereo, std::uint32_t frames,
                        const AudioListenerState& listener,
                        const AudioClipRegistry& clips, AudioVoicePool& voices,
                        const AudioMixerState& buses, AudioStatsBoard& stats)
{
    if (!m_initialized || interleavedStereo == nullptr || frames == 0) {
        return;
    }
    std::memset(interleavedStereo, 0, static_cast<std::size_t>(frames) * 2u * sizeof(float));
    if (buses.BusMuted(AudioBusId::Master)) {
        stats.peakMaster.store(0.0f, std::memory_order_relaxed);
        return;
    }
    m_busRack.Begin(frames);

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
    // Release spatializers whose voice is gone, keep the rest pinned to the
    // same slot, and mark occupancy so new voices only take genuinely free ones.
    m_spatialSlotUsed.assign(m_spatializers.size(), false);
    for (auto it = m_spatialAssignments.begin(); it != m_spatialAssignments.end();) {
        bool alive = false;
        for (const ActiveVoiceView& voice : m_activeVoices) {
            if (voice.params.spatial && VoiceKey(voice.handle) == it->first) {
                alive = true;
                break;
            }
        }
        if (!alive) {
            m_occlusionStates.erase(it->first);
            it = m_spatialAssignments.erase(it);
            continue;
        }
        if (it->second < m_spatialSlotUsed.size()) {
            m_spatialSlotUsed[it->second] = true;
        }
        ++it;
    }
    for (const ActiveVoiceView& voice : m_activeVoices) {
        const AudioClipDesc* desc = clips.Describe(voice.clip);
        const float* samples = clips.Samples(voice.clip);
        if (desc == nullptr || samples == nullptr || desc->frameCount == 0) {
            continue;
        }
        // The clip cursor advances at the same pitched rate the render loop
        // samples at; advancing by the block size regardless of pitch made
        // every block restart mid-ramp (a periodic discontinuity heard as
        // buzzing on any pitched or doppler-shifted voice).
        const float pitch = EffectivePitch(voice, listener);
        const std::uint32_t advanceFrames = std::max<std::uint32_t>(
            1u, static_cast<std::uint32_t>(static_cast<float>(frames) * pitch + 0.5f));
        if (buses.BusMuted(voice.params.bus)) {
            voices.Advance(voice.handle, advanceFrames, desc->frameCount);
            continue;
        }

        // Bus and master gains are applied later by the bus rack, with
        // ramping; the per-voice stage only bakes the voice's own gain.
        const float gain = voice.params.gain;
        if (gain <= 0.0f) {
            voices.Advance(voice.handle, advanceFrames, desc->frameCount);
            continue;
        }

        float* busBuffer = m_busRack.Buffer(voice.params.bus);

        if (desc->channels == 1) {
            std::fill(m_monoScratch.begin(), m_monoScratch.end(), 0.0f);
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
            if (voice.params.spatial) {
                const std::uint64_t key = VoiceKey(voice.handle);
                const float occlusionGain = ApplyOcclusion(
                    key, voice.params.source.occlusion, m_monoScratch.data(), frames);
                SpatialAudioListener spatialListener{};
                spatialListener.position = listener.position;
                spatialListener.right = listener.right;
                spatialListener.up = listener.up;
                spatialListener.forward = listener.forward;
                SpatialAudioSource spatialSource{};
                spatialSource.position = voice.params.source.position;
                spatialSource.gain = voice.params.source.gain * occlusionGain
                    * DistanceGain(voice.params.source, listener);
                spatialSource.spatialBlend = voice.params.spatialBlend;
                std::uint32_t slot = kNoSpatialSlot;
                if (const auto found = m_spatialAssignments.find(key);
                    found != m_spatialAssignments.end()) {
                    slot = found->second;
                } else {
                    for (std::uint32_t index = 0; index < m_spatialSlotUsed.size(); ++index) {
                        if (!m_spatialSlotUsed[index]) {
                            slot = index;
                            m_spatialSlotUsed[index] = true;
                            m_spatialAssignments.emplace(key, index);
                            break;
                        }
                    }
                }
                bool spatialized = false;
                if (slot != kNoSpatialSlot) {
                    spatialized = m_spatializers[slot].Process(
                        m_monoScratch, m_stereoScratch, spatialListener, spatialSource);
                }
                if (spatialized) {
                    AccumulateStereo(m_stereoScratch.data(), frames, gain, busBuffer);
                } else {
                    // Distance-attenuated centre pan: the voice stays audible
                    // when every spatializer is busy or HRTF processing rejects
                    // the input, instead of dropping out for the whole block.
                    const float fallbackGain = gain * spatialSource.gain * 0.70710678f;
                    for (std::uint32_t frame = 0; frame < frames; ++frame) {
                        const float sample = m_monoScratch[frame] * fallbackGain;
                        busBuffer[frame * 2u] += sample;
                        busBuffer[frame * 2u + 1u] += sample;
                    }
                }
            } else {
                for (std::uint32_t frame = 0; frame < frames; ++frame) {
                    const float sample = m_monoScratch[frame] * gain;
                    busBuffer[frame * 2u] += sample;
                    busBuffer[frame * 2u + 1u] += sample;
                }
            }
        } else {
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
        voices.Advance(voice.handle, advanceFrames, desc->frameCount);
    }

    m_busRack.MixDown(interleavedStereo, frames, buses, stats);
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
        const float angleDegrees = std::acos(cosine) * (180.0f / kPi);
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

float AudioMixer::ApplyOcclusion(std::uint64_t voiceKey, float occlusion,
                                 float* mono, std::uint32_t frames) noexcept
{
    if (!std::isfinite(occlusion) || occlusion <= 0.0f) {
        m_occlusionStates.erase(voiceKey);
        return 1.0f;
    }
    const float amount = std::min(occlusion, 1.0f);
    // Occluded sources lose highs first: sweep a one-pole low-pass from
    // fully open (~20 kHz) down to a muffled ~500 Hz as occlusion rises,
    // and shave overall level so a fully blocked source sits well behind
    // direct ones without vanishing entirely.
    const float cutoffHz = 20000.0f * std::pow(500.0f / 20000.0f, amount);
    const float sampleRate = static_cast<float>(m_config.device.sampleRate);
    const float coefficient = std::exp(-2.0f * kPi * cutoffHz / sampleRate);
    OcclusionState& state = m_occlusionStates[voiceKey];
    float history = state.left;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        history = coefficient * history + (1.0f - coefficient) * mono[frame];
        mono[frame] = history;
    }
    state.left = std::isfinite(history) ? history : 0.0f;
    return 1.0f - 0.7f * amount;
}

void AudioMixer::AccumulateStereo(const float* stereo, std::uint32_t frames,
                                  float gain, float* out) noexcept
{
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        out[frame * 2u] += stereo[frame * 2u] * gain;
        out[frame * 2u + 1u] += stereo[frame * 2u + 1u] * gain;
    }
}

} // namespace Concord::Audio::Detail
