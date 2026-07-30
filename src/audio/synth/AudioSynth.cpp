#include "audio/synth/AudioSynth.h"

#include <algorithm>
#include <cmath>

namespace Concord::Audio {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTau = 6.28318530717958647692f;

float WaveSample(AudioWaveform waveform, float phase, std::uint32_t& noiseState) noexcept
{
    switch (waveform) {
    case AudioWaveform::Sine:
        return std::sin(phase);
    case AudioWaveform::Square:
        return std::sin(phase) >= 0.0f ? 1.0f : -1.0f;
    case AudioWaveform::Triangle:
        return std::asin(std::sin(phase)) * (2.0f / kPi);
    case AudioWaveform::Saw: {
        const float wrapped = phase / kTau - std::floor(phase / kTau + 0.5f);
        return wrapped * 2.0f;
    }
    case AudioWaveform::Noise:
        noiseState = noiseState * 1664525u + 1013904223u;
        return static_cast<float>((noiseState >> 8) & 0x00FFFFFFu)
            / static_cast<float>(0x007FFFFFu) - 1.0f;
    }
    return 0.0f;
}

float EnvelopeGain(const AudioEnvelope& envelope, float time, float duration) noexcept
{
    const float attackEnd = std::max(0.0f, envelope.attackSeconds);
    const float decayEnd = attackEnd + std::max(0.0f, envelope.decaySeconds);
    const float releaseStart = std::max(0.0f, duration - std::max(0.0f, envelope.releaseSeconds));
    if (attackEnd > 0.0f && time < attackEnd) {
        return time / attackEnd;
    }
    if (time < decayEnd && decayEnd > attackEnd) {
        const float t = (time - attackEnd) / (decayEnd - attackEnd);
        return 1.0f + (envelope.sustainLevel - 1.0f) * t;
    }
    if (time < releaseStart) {
        return envelope.sustainLevel;
    }
    if (duration > releaseStart) {
        const float t = (time - releaseStart) / (duration - releaseStart);
        return envelope.sustainLevel * (1.0f - std::clamp(t, 0.0f, 1.0f));
    }
    return 0.0f;
}

AudioSynthesisDesc BuiltIn(AudioBuiltInSound sound, std::int32_t sampleRate)
{
    AudioSynthesisDesc desc;
    desc.sampleRate = sampleRate;
    switch (sound) {
    case AudioBuiltInSound::Beep:
        desc.durationSeconds = 0.16f;
        desc.frequencyHz = 880.0f;
        desc.amplitude = 0.22f;
        break;
    case AudioBuiltInSound::Click:
        desc.waveform = AudioWaveform::Noise;
        desc.durationSeconds = 0.035f;
        desc.amplitude = 0.14f;
        desc.envelope.attackSeconds = 0.0f;
        desc.envelope.decaySeconds = 0.01f;
        desc.envelope.sustainLevel = 0.0f;
        desc.envelope.releaseSeconds = 0.02f;
        break;
    case AudioBuiltInSound::Confirm:
        desc.durationSeconds = 0.22f;
        desc.frequencyHz = 660.0f;
        desc.endFrequencyHz = 990.0f;
        desc.amplitude = 0.20f;
        break;
    case AudioBuiltInSound::Error:
        desc.durationSeconds = 0.28f;
        desc.frequencyHz = 320.0f;
        desc.endFrequencyHz = 180.0f;
        desc.waveform = AudioWaveform::Saw;
        desc.amplitude = 0.22f;
        break;
    case AudioBuiltInSound::Powerup:
        desc.durationSeconds = 0.42f;
        desc.frequencyHz = 220.0f;
        desc.endFrequencyHz = 1180.0f;
        desc.waveform = AudioWaveform::Triangle;
        desc.amplitude = 0.22f;
        break;
    case AudioBuiltInSound::Laser:
        desc.durationSeconds = 0.18f;
        desc.frequencyHz = 1400.0f;
        desc.endFrequencyHz = 420.0f;
        desc.waveform = AudioWaveform::Saw;
        desc.amplitude = 0.18f;
        break;
    case AudioBuiltInSound::Explosion:
        desc.durationSeconds = 0.65f;
        desc.frequencyHz = 120.0f;
        desc.endFrequencyHz = 35.0f;
        desc.waveform = AudioWaveform::Noise;
        desc.amplitude = 0.30f;
        desc.envelope.attackSeconds = 0.0f;
        desc.envelope.decaySeconds = 0.18f;
        desc.envelope.sustainLevel = 0.35f;
        desc.envelope.releaseSeconds = 0.28f;
        break;
    case AudioBuiltInSound::EngineHum:
        desc.durationSeconds = 1.0f;
        desc.frequencyHz = 90.0f;
        desc.endFrequencyHz = 110.0f;
        desc.waveform = AudioWaveform::Triangle;
        desc.vibratoHz = 2.0f;
        desc.vibratoDepth = 0.02f;
        desc.amplitude = 0.18f;
        desc.envelope.attackSeconds = 0.02f;
        desc.envelope.decaySeconds = 0.08f;
        desc.envelope.sustainLevel = 0.85f;
        desc.envelope.releaseSeconds = 0.12f;
        break;
    }
    return desc;
}

} // namespace

AudioSynthBuffer AudioSynth::Generate(const AudioSynthesisDesc& desc)
{
    AudioSynthBuffer buffer;
    if (desc.sampleRate <= 0 || (desc.channels != 1 && desc.channels != 2)
        || !std::isfinite(desc.durationSeconds) || desc.durationSeconds <= 0.0f
        || !std::isfinite(desc.frequencyHz) || !std::isfinite(desc.endFrequencyHz)
        || !std::isfinite(desc.amplitude)) {
        return buffer;
    }
    const std::size_t frames = static_cast<std::size_t>(std::max(1, static_cast<int>(
        desc.durationSeconds * static_cast<float>(desc.sampleRate))));
    buffer.sampleRate = desc.sampleRate;
    buffer.channels = desc.channels;
    buffer.samples.resize(frames * desc.channels);
    float phase = 0.0f;
    std::uint32_t noiseState = desc.noiseSeed == 0 ? 1u : desc.noiseSeed;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const float t = static_cast<float>(frame) / static_cast<float>(desc.sampleRate);
        const float lerp = frames > 1 ? static_cast<float>(frame) / static_cast<float>(frames - 1) : 0.0f;
        float frequency = desc.frequencyHz + (desc.endFrequencyHz - desc.frequencyHz) * lerp;
        if (desc.vibratoHz > 0.0f && desc.vibratoDepth != 0.0f) {
            frequency *= 1.0f + std::sin(kTau * desc.vibratoHz * t) * desc.vibratoDepth;
        }
        phase += kTau * frequency / static_cast<float>(desc.sampleRate);
        float sample = WaveSample(desc.waveform, phase, noiseState);
        if (desc.tremoloHz > 0.0f && desc.tremoloDepth != 0.0f) {
            sample *= 1.0f + std::sin(kTau * desc.tremoloHz * t) * desc.tremoloDepth;
        }
        sample *= EnvelopeGain(desc.envelope, t, desc.durationSeconds);
        sample *= desc.amplitude;
        sample = std::clamp(sample, -1.0f, 1.0f);
        if (desc.channels == 1) {
            buffer.samples[frame] = sample;
        } else {
            buffer.samples[frame * 2u] = sample;
            buffer.samples[frame * 2u + 1u] = sample;
        }
    }
    return buffer;
}

AudioSynthBuffer AudioSynth::GenerateBuiltIn(AudioBuiltInSound sound,
                                             std::int32_t sampleRate)
{
    return Generate(BuiltIn(sound, sampleRate));
}

} // namespace Concord::Audio
