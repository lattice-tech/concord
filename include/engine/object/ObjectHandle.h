#ifndef CONCORD_OBJECTHANDLE_H
#define CONCORD_OBJECTHANDLE_H

#include <cstdint>
#include <functional>
#include <limits>

namespace Concord::Object {

/** @brief Scene-qualified, generation-safe identity for one runtime node. */
struct ObjectHandle {
    std::uint64_t scene = 0;
    std::uint32_t slot = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t generation = 0;

    /** True when every identity component contains a usable value. */
    constexpr bool IsValid() const noexcept
    {
        return scene != 0
            && slot != std::numeric_limits<std::uint32_t>::max()
            && generation != 0;
    }

    friend constexpr bool operator==(ObjectHandle, ObjectHandle) noexcept = default;
};

inline constexpr ObjectHandle kInvalidObjectHandle{};

} // namespace Concord::Object

template <>
struct std::hash<Concord::Object::ObjectHandle> {
    std::size_t operator()(const Concord::Object::ObjectHandle& handle) const noexcept
    {
        const std::size_t a = std::hash<std::uint64_t>{}(handle.scene);
        const std::size_t b = std::hash<std::uint32_t>{}(handle.slot);
        const std::size_t c = std::hash<std::uint32_t>{}(handle.generation);
        return a ^ (b << 1) ^ (c << 2);
    }
};

#endif // CONCORD_OBJECTHANDLE_H
