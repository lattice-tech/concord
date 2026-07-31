#ifndef CONCORD_AUDIOEFFECTCHAIN_H
#define CONCORD_AUDIOEFFECTCHAIN_H

#include "audio/effects/AudioEffectDesc.h"
#include "audio/effects/IAudioEffect.h"

#include <memory>
#include <span>
#include <vector>

namespace Concord::Audio::Detail {

/**
 * @brief Ordered list of insert effects applied in place to one bus buffer.
 *
 * The chain owns its effect instances. Rebuild allocates and therefore only
 * runs when the bus configuration actually changed (tracked by revision in
 * the mixer), never on the steady-state render path.
 */
class AudioEffectChain {
public:
    /** Replaces the chain with effects built from `descs`; skips failures. */
    void Rebuild(std::span<const AudioEffectDesc> descs, std::uint32_t sampleRate);
    void Clear() noexcept;
    void Reset() noexcept;
    bool Empty() const noexcept { return m_effects.empty(); }
    void Process(float* interleavedStereo, std::uint32_t frames);

private:
    std::vector<std::unique_ptr<IAudioEffect>> m_effects;
};

} // namespace Concord::Audio::Detail

#endif // CONCORD_AUDIOEFFECTCHAIN_H
