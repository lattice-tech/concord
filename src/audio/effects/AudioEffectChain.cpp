#include "audio/effects/detail/AudioEffectChain.h"

#include "audio/effects/detail/AudioEffectFactory.h"

namespace Concord::Audio::Detail {

void AudioEffectChain::Rebuild(std::span<const AudioEffectDesc> descs,
                               std::uint32_t sampleRate)
{
    Clear();
    m_effects.reserve(descs.size());
    for (const AudioEffectDesc& desc : descs) {
        std::unique_ptr<IAudioEffect> effect = CreateEffect(desc, sampleRate);
        if (effect) {
            m_effects.push_back(std::move(effect));
        }
    }
}

void AudioEffectChain::Clear() noexcept
{
    for (auto& effect : m_effects) {
        effect->Shutdown();
    }
    m_effects.clear();
}

void AudioEffectChain::Reset() noexcept
{
    for (auto& effect : m_effects) {
        effect->Reset();
    }
}

void AudioEffectChain::Process(float* interleavedStereo, std::uint32_t frames)
{
    for (auto& effect : m_effects) {
        effect->Process(interleavedStereo, frames);
    }
}

} // namespace Concord::Audio::Detail
