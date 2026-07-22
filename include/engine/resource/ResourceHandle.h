#ifndef CONCORD_RESOURCEHANDLE_H
#define CONCORD_RESOURCEHANDLE_H

#include <cstdint>

namespace Concord {

/**
 * @brief Generation-checked reference to a slot in a ResourcePool.
 *
 * The owning pool validates both fields. `Tag` prevents handles for different
 * resource types from being interchanged.
 */
template <typename Tag>
struct ResourceHandle {
    /** Slot index in the owning pool. */
    std::uint32_t index = 0;

    /** Slot generation at creation time; 0 is reserved for the invalid handle. */
    std::uint32_t generation = 0;

    /** Returns the reserved invalid handle. */
    static constexpr ResourceHandle Invalid() noexcept { return ResourceHandle{0, 0}; }

    /** False for the invalid handle; a true result does not guarantee the slot is still live. */
    bool IsValid() const noexcept { return generation != 0; }

    friend bool operator==(const ResourceHandle& lhs, const ResourceHandle& rhs) noexcept
    {
        return lhs.index == rhs.index && lhs.generation == rhs.generation;
    }

    friend bool operator!=(const ResourceHandle& lhs, const ResourceHandle& rhs) noexcept
    {
        return !(lhs == rhs);
    }
};

} // namespace Concord

#endif // CONCORD_RESOURCEHANDLE_H
