#ifndef CONCORD_PERSISTENTOBJECTID_H
#define CONCORD_PERSISTENTOBJECTID_H

#include <cstdint>

namespace Concord::Object {

/** @brief Stable scene identity serialized across save/load and migration. */
struct PersistentObjectId {
    std::uint64_t value = 0;

    constexpr bool IsValid() const noexcept { return value != 0; }
    friend constexpr bool operator==(PersistentObjectId,
                                     PersistentObjectId) noexcept = default;
};

inline constexpr PersistentObjectId kInvalidPersistentObjectId{};

} // namespace Concord::Object

#endif // CONCORD_PERSISTENTOBJECTID_H
