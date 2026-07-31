#include "audio/runtime/detail/AudioCommandQueue.h"

#include "audio/runtime/detail/AudioMixerState.h"
#include "audio/runtime/detail/AudioVoicePool.h"

#include <bit>

namespace Concord::Audio::Detail {

void AudioCommandQueue::Init(std::uint32_t capacity)
{
    const std::uint32_t rounded = std::bit_ceil(capacity < 2u ? 2u : capacity);
    m_slots.assign(rounded, AudioCommand{});
    m_mask = rounded - 1u;
    m_head.store(0, std::memory_order_relaxed);
    m_tail.store(0, std::memory_order_relaxed);
}

void AudioCommandQueue::Reset() noexcept
{
    m_head.store(0, std::memory_order_relaxed);
    m_tail.store(0, std::memory_order_relaxed);
}

bool AudioCommandQueue::TryPush(const AudioCommand& command) noexcept
{
    if (m_slots.empty()) {
        return false;
    }
    const std::uint64_t tail = m_tail.load(std::memory_order_relaxed);
    const std::uint64_t head = m_head.load(std::memory_order_acquire);
    if (tail - head > m_mask) {
        return false;
    }
    m_slots[static_cast<std::size_t>(tail & m_mask)] = command;
    m_tail.store(tail + 1, std::memory_order_release);
    return true;
}

bool AudioCommandQueue::TryPop(AudioCommand& out) noexcept
{
    if (m_slots.empty()) {
        return false;
    }
    const std::uint64_t head = m_head.load(std::memory_order_relaxed);
    const std::uint64_t tail = m_tail.load(std::memory_order_acquire);
    if (head == tail) {
        return false;
    }
    out = m_slots[static_cast<std::size_t>(head & m_mask)];
    m_head.store(head + 1, std::memory_order_release);
    return true;
}

std::uint32_t AudioCommandQueue::ApproxDepth() const noexcept
{
    const std::uint64_t head = m_head.load(std::memory_order_relaxed);
    const std::uint64_t tail = m_tail.load(std::memory_order_relaxed);
    return tail >= head ? static_cast<std::uint32_t>(tail - head) : 0u;
}

namespace {

template <typename Mutator>
void PatchSource(AudioVoicePool& voices, AudioVoiceHandle voice,
                 Mutator mutator) noexcept
{
    AudioSourceState current;
    if (!voices.QuerySource(voice, current)) {
        return;
    }
    mutator(current);
    voices.SetSpatialState(voice, current);
}

} // namespace

void ApplyAudioCommand(const AudioCommand& command, AudioListenerState& listener,
                       AudioVoicePool& voices, AudioMixerState& mixerState) noexcept
{
    switch (command.kind) {
    case AudioCommandKind::SetListener:
        listener = command.listener;
        break;
    case AudioCommandKind::SetVoiceGain:
        voices.SetGain(command.voice, command.value);
        break;
    case AudioCommandKind::SetVoicePitch:
        voices.SetPitch(command.voice, command.value);
        break;
    case AudioCommandKind::SetVoiceBus:
        voices.SetBus(command.voice, command.bus);
        break;
    case AudioCommandKind::SetVoiceSpatialBlend:
        voices.SetSpatialBlend(command.voice, command.value);
        break;
    case AudioCommandKind::SetVoiceSpatialState:
        voices.SetSpatialState(command.voice, command.source);
        break;
    case AudioCommandKind::PatchVoicePosition:
        PatchSource(voices, command.voice, [&command](AudioSourceState& source) {
            source.position = command.vec;
        });
        break;
    case AudioCommandKind::PatchVoiceForward:
        PatchSource(voices, command.voice, [&command](AudioSourceState& source) {
            source.forward = command.vec;
        });
        break;
    case AudioCommandKind::PatchVoiceVelocity:
        PatchSource(voices, command.voice, [&command](AudioSourceState& source) {
            source.velocity = command.vec;
        });
        break;
    case AudioCommandKind::PatchVoiceOcclusion:
        PatchSource(voices, command.voice, [&command](AudioSourceState& source) {
            source.occlusion = command.value;
        });
        break;
    case AudioCommandKind::SetBusGain:
        mixerState.SetBusGain(command.bus, command.value);
        break;
    case AudioCommandKind::SetBusMute:
        mixerState.SetBusMute(command.bus, command.flag);
        break;
    }
}

void DrainAudioCommands(AudioCommandQueue& queue, AudioListenerState& listener,
                        AudioVoicePool& voices, AudioMixerState& mixerState,
                        AudioStatsBoard& stats) noexcept
{
    AudioCommand command;
    while (queue.TryPop(command)) {
        ApplyAudioCommand(command, listener, voices, mixerState);
    }
    stats.queuedCommands.store(queue.ApproxDepth(), std::memory_order_relaxed);
}

} // namespace Concord::Audio::Detail
