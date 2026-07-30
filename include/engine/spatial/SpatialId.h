#ifndef CONCORD_SPATIALID_H
#define CONCORD_SPATIALID_H

#include <cstdint>
#include <limits>

namespace Concord::Spatial {

/**
 * @brief Generation-safe identity for one proxy in a DynamicAabbTree.
 *
 * Slot indices may be reused after removal; the generation distinguishes a
 * live proxy from a stale handle that once named the same slot. The default
 * value is invalid and never names a live proxy.
 */
struct SpatialId {
    std::uint32_t slot = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t generation = 0;

    constexpr bool IsValid() const noexcept
    {
        return slot != std::numeric_limits<std::uint32_t>::max() && generation != 0;
    }

    friend constexpr bool operator==(SpatialId, SpatialId) noexcept = default;
};

inline constexpr SpatialId kInvalidSpatialId{};

} // namespace Concord::Spatial

#endif // CONCORD_SPATIALID_H
