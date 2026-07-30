#ifndef CONCORD_AUDIOVOICE_H
#define CONCORD_AUDIOVOICE_H

#include "Concord/CExport.h"

#include <cstdint>

namespace Concord::Audio {

/** Generation-safe handle naming one runtime voice stored by CAudio.dll. */
class CAUDIO_API AudioVoiceHandle {
public:
    constexpr AudioVoiceHandle() = default;
    constexpr AudioVoiceHandle(std::uint32_t slot, std::uint32_t generation) noexcept
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

#endif // CONCORD_AUDIOVOICE_H
