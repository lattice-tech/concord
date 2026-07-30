#ifndef CONCORD_AUDIOCLIPREGISTRY_H
#define CONCORD_AUDIOCLIPREGISTRY_H

#include "audio/runtime/AudioClip.h"

#include <span>
#include <vector>

namespace Concord::Audio::Detail {

class AudioClipRegistry {
public:
    void Reset(std::uint32_t capacity);
    AudioClipHandle CreateFromPcm(const AudioClipDesc& desc,
                                  std::span<const float> samples);
    bool Destroy(AudioClipHandle handle);
    bool IsAlive(AudioClipHandle handle) const noexcept;
    const AudioClipDesc* Describe(AudioClipHandle handle) const noexcept;
    const float* Samples(AudioClipHandle handle) const noexcept;

private:
    struct Slot {
        AudioClipDesc desc{};
        std::vector<float> samples;
        std::uint32_t generation = 1;
        bool occupied = false;
    };

    Slot* Resolve(AudioClipHandle handle) noexcept;
    const Slot* Resolve(AudioClipHandle handle) const noexcept;

    std::vector<Slot> m_slots;
};

} // namespace Concord::Audio::Detail

#endif // CONCORD_AUDIOCLIPREGISTRY_H
