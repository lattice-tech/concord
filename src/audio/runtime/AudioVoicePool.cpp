#include "audio/runtime/detail/AudioVoicePool.h"

#include "audio/runtime/detail/AudioValidation.h"

#include <cmath>

namespace Concord::Audio::Detail {

void AudioVoicePool::Reset(std::uint32_t capacity)
{
    m_slots.clear();
    m_slots.resize(capacity);
}

AudioVoiceHandle AudioVoicePool::Play(AudioClipHandle clip, const AudioPlayParams& params,
                                      bool loop, const AudioClipRegistry& clips,
                                      std::uint64_t& rejectedCommands)
{
    const AudioClipDesc* desc = clips.Describe(clip);
    if (desc == nullptr || !std::isfinite(params.gain) || !std::isfinite(params.pitch)
        || params.gain < 0.0f || params.pitch <= 0.0f
        || !IsFinite(params.source)) {
        return {};
    }
    if (params.spatial && (desc->channels != 1 || !desc->spatializable)) {
        return {};
    }
    for (std::uint32_t slot = 0; slot < m_slots.size(); ++slot) {
        Slot& voice = m_slots[slot];
        if (voice.active) {
            continue;
        }
        voice.clip = clip;
        voice.params = params;
        voice.params.loop = loop;
        voice.cursorFrame = 0;
        voice.active = true;
        voice.paused = false;
        return AudioVoiceHandle(slot, voice.generation);
    }
    ++rejectedCommands;
    return {};
}

AudioVoiceHandle AudioVoicePool::StealOrPlay(AudioClipHandle clip, const AudioPlayParams& params,
                                             bool loop, const AudioClipRegistry& clips,
                                             std::uint64_t& rejectedCommands)
{
    if (AudioVoiceHandle handle = Play(clip, params, loop, clips, rejectedCommands);
        handle.IsValid()) {
        return handle;
    }
    Slot* candidate = FindStealCandidate(params.priority);
    if (candidate == nullptr) {
        return {};
    }
    ++candidate->generation;
    if (candidate->generation == 0) {
        candidate->generation = 1;
    }
    candidate->clip = clip;
    candidate->params = params;
    candidate->params.loop = loop;
    candidate->cursorFrame = 0;
    candidate->active = true;
    candidate->paused = false;
    return AudioVoiceHandle(static_cast<std::uint32_t>(candidate - m_slots.data()),
                            candidate->generation);
}

bool AudioVoicePool::Stop(AudioVoiceHandle handle)
{
    Slot* voice = Resolve(handle);
    if (voice == nullptr) {
        return false;
    }
    voice->active = false;
    ++voice->generation;
    if (voice->generation == 0) {
        voice->generation = 1;
    }
    return true;
}

bool AudioVoicePool::Pause(AudioVoiceHandle handle, bool paused)
{
    Slot* voice = Resolve(handle);
    if (voice == nullptr) {
        return false;
    }
    voice->paused = paused;
    return true;
}

void AudioVoicePool::StopAll(AudioBusId bus)
{
    for (Slot& voice : m_slots) {
        if (!voice.active || voice.params.bus != bus) {
            continue;
        }
        voice.active = false;
        voice.paused = false;
        ++voice.generation;
        if (voice.generation == 0) {
            voice.generation = 1;
        }
    }
}

bool AudioVoicePool::SetGain(AudioVoiceHandle handle, float gain)
{
    Slot* voice = Resolve(handle);
    if (voice == nullptr || !std::isfinite(gain) || gain < 0.0f) {
        return false;
    }
    voice->params.gain = gain;
    return true;
}

bool AudioVoicePool::SetPitch(AudioVoiceHandle handle, float pitch)
{
    Slot* voice = Resolve(handle);
    if (voice == nullptr || !std::isfinite(pitch) || pitch <= 0.0f) {
        return false;
    }
    voice->params.pitch = pitch;
    return true;
}

bool AudioVoicePool::SetBus(AudioVoiceHandle handle, AudioBusId bus)
{
    Slot* voice = Resolve(handle);
    if (voice == nullptr) {
        return false;
    }
    voice->params.bus = bus;
    return true;
}

bool AudioVoicePool::SetSpatialBlend(AudioVoiceHandle handle, float spatialBlend)
{
    Slot* voice = Resolve(handle);
    if (voice == nullptr || !std::isfinite(spatialBlend)) {
        return false;
    }
    voice->params.spatialBlend = spatialBlend;
    voice->params.source.spatialBlend = spatialBlend;
    return true;
}

bool AudioVoicePool::SetSpatialState(AudioVoiceHandle handle, const AudioSourceState& source)
{
    Slot* voice = Resolve(handle);
    if (voice == nullptr || !IsFinite(source)) {
        return false;
    }
    voice->params.source = source;
    voice->params.spatialBlend = source.spatialBlend;
    return true;
}

bool AudioVoicePool::QuerySource(AudioVoiceHandle handle, AudioSourceState& out) const noexcept
{
    if (!handle.IsValid() || handle.Slot() >= m_slots.size()) {
        return false;
    }
    const Slot& voice = m_slots[handle.Slot()];
    if (!voice.active || voice.generation != handle.Generation()) {
        return false;
    }
    out = voice.params.source;
    return true;
}

void AudioVoicePool::StopVoicesUsingClip(AudioClipHandle clip)
{
    for (Slot& voice : m_slots) {
        if (!voice.active || voice.clip.Slot() != clip.Slot()
            || voice.clip.Generation() != clip.Generation()) {
            continue;
        }
        voice.active = false;
        voice.paused = false;
        ++voice.generation;
        if (voice.generation == 0) {
            voice.generation = 1;
        }
    }
}

void AudioVoicePool::CollectActive(std::vector<ActiveVoiceView>& out) const
{
    out.clear();
    out.reserve(m_slots.size());
    for (std::uint32_t slot = 0; slot < m_slots.size(); ++slot) {
        const Slot& voice = m_slots[slot];
        if (!voice.active) {
            continue;
        }
        out.push_back(ActiveVoiceView{
            AudioVoiceHandle(slot, voice.generation),
            voice.clip,
            voice.params,
            voice.cursorFrame,
        });
    }
}

void AudioVoicePool::Advance(AudioVoiceHandle handle, std::uint32_t frames, std::uint32_t clipFrames)
{
    Slot* voice = Resolve(handle);
    if (voice == nullptr || clipFrames == 0) {
        return;
    }
    if (voice->paused) {
        return;
    }
    const std::uint64_t nextFrame = voice->cursorFrame + frames;
    if (voice->params.loop) {
        voice->cursorFrame = nextFrame % clipFrames;
        return;
    }
    if (nextFrame >= clipFrames) {
        voice->active = false;
        voice->paused = false;
        ++voice->generation;
        if (voice->generation == 0) {
            voice->generation = 1;
        }
        voice->cursorFrame = 0;
        return;
    }
    voice->cursorFrame = nextFrame;
}

bool AudioVoicePool::IsAlive(AudioVoiceHandle handle) const noexcept
{
    if (!handle.IsValid() || handle.Slot() >= m_slots.size()) {
        return false;
    }
    const Slot& voice = m_slots[handle.Slot()];
    return voice.active && voice.generation == handle.Generation();
}

std::uint32_t AudioVoicePool::ActiveVoiceCount() const noexcept
{
    std::uint32_t count = 0;
    for (const Slot& voice : m_slots) {
        if (voice.active) {
            ++count;
        }
    }
    return count;
}

std::uint32_t AudioVoicePool::ActiveSpatialVoiceCount() const noexcept
{
    std::uint32_t count = 0;
    for (const Slot& voice : m_slots) {
        if (voice.active && voice.params.spatial) {
            ++count;
        }
    }
    return count;
}

AudioVoicePool::Slot* AudioVoicePool::Resolve(AudioVoiceHandle handle) noexcept
{
    if (!handle.IsValid() || handle.Slot() >= m_slots.size()) {
        return nullptr;
    }
    Slot& voice = m_slots[handle.Slot()];
    if (!voice.active || voice.generation != handle.Generation()) {
        return nullptr;
    }
    return &voice;
}

AudioVoicePool::Slot* AudioVoicePool::FindStealCandidate(std::uint8_t incomingPriority) noexcept
{
    Slot* best = nullptr;
    for (Slot& voice : m_slots) {
        if (!voice.active) {
            continue;
        }
        if (voice.params.priority > incomingPriority) {
            continue;
        }
        if (best == nullptr
            || voice.params.priority < best->params.priority
            || (voice.params.priority == best->params.priority
                && voice.paused && !best->paused)
            || (voice.params.priority == best->params.priority
                && voice.params.spatial != best->params.spatial
                && !voice.params.spatial)
            || (voice.params.priority == best->params.priority
                && voice.params.loop != best->params.loop
                && !voice.params.loop)) {
            best = &voice;
        }
    }
    return best;
}

} // namespace Concord::Audio::Detail
