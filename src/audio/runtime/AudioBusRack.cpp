#include "audio/runtime/detail/AudioBusRack.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Concord::Audio::Detail {

namespace {

float EnvelopeCoefficient(float seconds, std::uint32_t frames,
                          std::uint32_t sampleRate) noexcept
{
    const float clamped = std::max(seconds, 0.001f);
    const float blockSeconds = static_cast<float>(frames)
        / static_cast<float>(sampleRate);
    return std::exp(-blockSeconds / clamped);
}

} // namespace

bool AudioBusRack::Init(const AudioRuntimeConfig& config)
{
    Shutdown();
    const std::size_t stereoSamples = static_cast<std::size_t>(config.device.frameSize) * 2u;
    for (auto& buffer : m_busBuffers) {
        buffer.assign(stereoSamples, 0.0f);
    }
    m_currentGains.fill(1.0f);
    m_busPeaks.fill(0.0f);
    m_sampleRate = static_cast<std::uint32_t>(config.device.sampleRate);
    if (!m_masterLimiter.Init(m_sampleRate)) {
        return false;
    }
    m_effectsRevisionSeen = 0;
    m_duckingRevisionSeen = 0;
    m_initialized = true;
    return true;
}

void AudioBusRack::Shutdown() noexcept
{
    for (auto& buffer : m_busBuffers) {
        buffer.clear();
    }
    for (auto& chain : m_busChains) {
        chain.Clear();
    }
    m_duckEnvelopes.clear();
    m_masterLimiter.Shutdown();
    m_initialized = false;
}

void AudioBusRack::Begin(std::uint32_t frames) noexcept
{
    const std::size_t bytes = static_cast<std::size_t>(frames) * 2u * sizeof(float);
    for (auto& buffer : m_busBuffers) {
        std::memset(buffer.data(), 0, bytes);
    }
}

void AudioBusRack::MixDown(float* interleavedStereo, std::uint32_t frames,
                           const AudioMixerState& state, AudioStatsBoard& stats)
{
    if (!m_initialized) {
        return;
    }
    SyncConfig(state);

    for (std::size_t busIndex = 0; busIndex < kBusCount; ++busIndex) {
        const AudioBusId bus = static_cast<AudioBusId>(busIndex);
        float* buffer = m_busBuffers[busIndex].data();
        if (!m_busChains[busIndex].Empty() && !state.BusMuted(bus)) {
            m_busChains[busIndex].Process(buffer, frames);
        }
        m_busPeaks[busIndex] = PeakLevel(buffer, frames);
    }

    UpdateDucking(state, frames);

    float* master = m_busBuffers[static_cast<std::size_t>(AudioBusId::Master)].data();
    for (std::size_t busIndex = 0; busIndex < kBusCount; ++busIndex) {
        const AudioBusId bus = static_cast<AudioBusId>(busIndex);
        if (bus == AudioBusId::Master) {
            continue;
        }
        const float target = state.BusMuted(bus)
            ? 0.0f
            : std::max(state.BusGain(bus), 0.0f) * DuckFactor(state, bus);
        const float gainBegin = m_currentGains[busIndex];
        const float gainEnd = RampGain(busIndex, target,
                                       state.GainFadeSeconds(), frames);
        AccumulateRamped(m_busBuffers[busIndex].data(), master, frames,
                         gainBegin, gainEnd);
    }

    const std::size_t masterIndex = static_cast<std::size_t>(AudioBusId::Master);
    const float masterTarget = state.BusMuted(AudioBusId::Master)
        ? 0.0f : std::max(state.BusGain(AudioBusId::Master), 0.0f);
    const float masterBegin = m_currentGains[masterIndex];
    const float masterEnd = RampGain(masterIndex, masterTarget,
                                     state.GainFadeSeconds(), frames);
    std::memset(interleavedStereo, 0,
                static_cast<std::size_t>(frames) * 2u * sizeof(float));
    AccumulateRamped(master, interleavedStereo, frames, masterBegin, masterEnd);

    m_masterLimiter.Process(interleavedStereo, frames);

    stats.peakMusic.store(m_busPeaks[static_cast<std::size_t>(AudioBusId::Music)],
                          std::memory_order_relaxed);
    stats.peakSfx.store(m_busPeaks[static_cast<std::size_t>(AudioBusId::Sfx)],
                        std::memory_order_relaxed);
    stats.peakUi.store(m_busPeaks[static_cast<std::size_t>(AudioBusId::Ui)],
                       std::memory_order_relaxed);
    stats.peakDialogue.store(m_busPeaks[static_cast<std::size_t>(AudioBusId::Dialogue)],
                             std::memory_order_relaxed);
    stats.peakAmbience.store(m_busPeaks[static_cast<std::size_t>(AudioBusId::Ambience)],
                             std::memory_order_relaxed);
    stats.peakMaster.store(PeakLevel(interleavedStereo, frames), std::memory_order_relaxed);
}

void AudioBusRack::SyncConfig(const AudioMixerState& state)
{
    if (state.EffectsRevision() != m_effectsRevisionSeen) {
        for (std::size_t busIndex = 0; busIndex < kBusCount; ++busIndex) {
            const auto& descs = state.BusEffects(static_cast<AudioBusId>(busIndex));
            m_busChains[busIndex].Rebuild(descs, m_sampleRate);
        }
        m_effectsRevisionSeen = state.EffectsRevision();
    }
    if (state.DuckingRevision() != m_duckingRevisionSeen) {
        m_duckEnvelopes.assign(state.DuckingRules().size(), 0.0f);
        m_duckingRevisionSeen = state.DuckingRevision();
    }
}

void AudioBusRack::UpdateDucking(const AudioMixerState& state, std::uint32_t frames)
{
    const std::vector<AudioDuckingDesc>& rules = state.DuckingRules();
    if (m_duckEnvelopes.size() != rules.size()) {
        m_duckEnvelopes.assign(rules.size(), 0.0f);
    }
    for (std::size_t index = 0; index < rules.size(); ++index) {
        const AudioDuckingDesc& rule = rules[index];
        const float triggerPeak = m_busPeaks[static_cast<std::size_t>(rule.trigger)];
        const bool triggered = triggerPeak > std::max(rule.thresholdLinear, 0.0f);
        const float target = triggered ? 1.0f : 0.0f;
        const float coefficient = EnvelopeCoefficient(
            triggered ? rule.attackSeconds : rule.releaseSeconds, frames, m_sampleRate);
        m_duckEnvelopes[index] = coefficient * m_duckEnvelopes[index]
            + (1.0f - coefficient) * target;
    }
}

float AudioBusRack::DuckFactor(const AudioMixerState& state, AudioBusId bus) const noexcept
{
    const std::vector<AudioDuckingDesc>& rules = state.DuckingRules();
    float factor = 1.0f;
    for (std::size_t index = 0; index < rules.size()
         && index < m_duckEnvelopes.size(); ++index) {
        if (rules[index].target != bus) {
            continue;
        }
        const float ducked = std::clamp(rules[index].duckedGain, 0.0f, 1.0f);
        factor *= 1.0f + (ducked - 1.0f) * m_duckEnvelopes[index];
    }
    return factor;
}

float AudioBusRack::RampGain(std::size_t busIndex, float target, float fadeSeconds,
                             std::uint32_t frames) noexcept
{
    const float current = m_currentGains[busIndex];
    const float blockSeconds = static_cast<float>(frames)
        / static_cast<float>(m_sampleRate);
    float next = target;
    if (fadeSeconds > blockSeconds) {
        const float maxStep = blockSeconds / fadeSeconds;
        const float difference = target - current;
        const float step = std::clamp(difference, -maxStep, maxStep);
        next = current + step;
        if (std::abs(target - next) < 1.0e-4f) {
            next = target;
        }
    }
    m_currentGains[busIndex] = next;
    return next;
}

float AudioBusRack::PeakLevel(const float* stereo, std::uint32_t frames) noexcept
{
    float peak = 0.0f;
    for (std::uint32_t sample = 0; sample < frames * 2u; ++sample) {
        peak = std::max(peak, std::abs(stereo[sample]));
    }
    return peak;
}

void AudioBusRack::AccumulateRamped(const float* source, float* destination,
                                    std::uint32_t frames, float gainBegin,
                                    float gainEnd) noexcept
{
    if (gainBegin <= 0.0f && gainEnd <= 0.0f) {
        return;
    }
    const float step = frames > 1
        ? (gainEnd - gainBegin) / static_cast<float>(frames - 1) : 0.0f;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        const float gain = gainBegin + step * static_cast<float>(frame);
        destination[frame * 2u] += source[frame * 2u] * gain;
        destination[frame * 2u + 1u] += source[frame * 2u + 1u] * gain;
    }
}

} // namespace Concord::Audio::Detail
