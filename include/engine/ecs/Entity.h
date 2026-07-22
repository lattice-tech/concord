#ifndef CONCORD_ENTITY_H
#define CONCORD_ENTITY_H

#include <cstdint>

namespace Concord::Ecs {

/** Stable entity handle; generation rejects stale references after destruction. */
struct Entity {
    std::uint32_t index = UINT32_MAX;
    std::uint32_t generation = 0;

    bool IsValid() const noexcept { return index != UINT32_MAX; }
    friend bool operator==(Entity, Entity) = default;
};

inline constexpr Entity kInvalidEntity{};

} // namespace Concord::Ecs

#endif // CONCORD_ENTITY_H
