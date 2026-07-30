#ifndef CONCORD_AUDIOCLIP_H
#define CONCORD_AUDIOCLIP_H

#include "Concord/CExport.h"

#include <cstdint>

namespace Concord::Audio {

/** Immutable description of one clip's decoded PCM format. */
struct AudioClipDesc {
    std::int32_t sampleRate = 48000;
    std::uint8_t channels = 1;
    std::uint32_t frameCount = 0;
    bool streaming = false;
    bool spatializable = true;
};

/** Generation-safe handle naming one clip stored by CAudio.dll. */
class CAUDIO_API AudioClipHandle {
public:
    constexpr AudioClipHandle() = default;
    constexpr AudioClipHandle(std::uint32_t slot, std::uint32_t generation) noexcept
        : m_slot(slot)
        , m_generation(generation)
    {
    }

    bool IsValid() const noexcept { return m_generation != 0; }
    std::uint32_t Slot() const noexcept { return m_slot; }
    std::uint32_t Generation() const noexcept { return m_generation; }

private:
    std::uint32_t m_slot = 0;
    std::uint32_t m_generation = 0;
};

} // namespace Concord::Audio

#endif // CONCORD_AUDIOCLIP_H
