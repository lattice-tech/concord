#include "audio/runtime/detail/AudioClipRegistry.h"

#include <cmath>

namespace Concord::Audio::Detail {

void AudioClipRegistry::Reset(std::uint32_t capacity)
{
    m_slots.clear();
    m_slots.resize(capacity);
}

AudioClipHandle AudioClipRegistry::CreateFromPcm(const AudioClipDesc& desc,
                                                 std::span<const float> samples)
{
    if (desc.sampleRate <= 0 || (desc.channels != 1 && desc.channels != 2)
        || desc.frameCount == 0 || samples.empty()) {
        return {};
    }
    const std::size_t expectedSamples = static_cast<std::size_t>(desc.frameCount)
        * static_cast<std::size_t>(desc.channels);
    if (samples.size() != expectedSamples) {
        return {};
    }
    for (float sample : samples) {
        if (!std::isfinite(sample)) {
            return {};
        }
    }
    for (std::uint32_t slot = 0; slot < m_slots.size(); ++slot) {
        Slot& clip = m_slots[slot];
        if (clip.occupied) {
            continue;
        }
        clip.desc = desc;
        clip.samples.assign(samples.begin(), samples.end());
        clip.occupied = true;
        return AudioClipHandle(slot, clip.generation);
    }
    return {};
}

bool AudioClipRegistry::Destroy(AudioClipHandle handle)
{
    Slot* clip = Resolve(handle);
    if (clip == nullptr) {
        return false;
    }
    clip->desc = {};
    clip->samples.clear();
    clip->occupied = false;
    ++clip->generation;
    if (clip->generation == 0) {
        clip->generation = 1;
    }
    return true;
}

bool AudioClipRegistry::IsAlive(AudioClipHandle handle) const noexcept
{
    return Resolve(handle) != nullptr;
}

const AudioClipDesc* AudioClipRegistry::Describe(AudioClipHandle handle) const noexcept
{
    const Slot* clip = Resolve(handle);
    return clip != nullptr ? &clip->desc : nullptr;
}

const float* AudioClipRegistry::Samples(AudioClipHandle handle) const noexcept
{
    const Slot* clip = Resolve(handle);
    return clip != nullptr && !clip->samples.empty() ? clip->samples.data() : nullptr;
}

AudioClipRegistry::Slot* AudioClipRegistry::Resolve(AudioClipHandle handle) noexcept
{
    if (!handle.IsValid() || handle.Slot() >= m_slots.size()) {
        return nullptr;
    }
    Slot& clip = m_slots[handle.Slot()];
    if (!clip.occupied || clip.generation != handle.Generation()) {
        return nullptr;
    }
    return &clip;
}

const AudioClipRegistry::Slot* AudioClipRegistry::Resolve(AudioClipHandle handle) const noexcept
{
    if (!handle.IsValid() || handle.Slot() >= m_slots.size()) {
        return nullptr;
    }
    const Slot& clip = m_slots[handle.Slot()];
    if (!clip.occupied || clip.generation != handle.Generation()) {
        return nullptr;
    }
    return &clip;
}

} // namespace Concord::Audio::Detail
