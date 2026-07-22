#ifndef CONCORD_EVENTTYPEID_H
#define CONCORD_EVENTTYPEID_H

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace Concord {

/**
 * @brief Stable value identity for one C++ event type within a compatible build.
 *
 * The value is derived from the compiler's canonical template signature, so the
 * EXE and DLL calculate the same identity without relying on module-local
 * addresses. It is a runtime notification identity, not a persistence or IPC ID.
 */
struct EventTypeId {
    std::uint64_t high = 0;
    std::uint64_t low = 0;

    friend constexpr bool operator==(EventTypeId, EventTypeId) noexcept = default;
};

namespace EventDetail {

struct EventTypeDescriptor {
    EventTypeId id;
    const char* name = nullptr;
    std::size_t nameLength = 0;
};

constexpr std::uint64_t HashTypeName(std::string_view name, std::uint64_t seed) noexcept
{
    std::uint64_t hash = seed;
    for (const unsigned char byte : name) {
        hash ^= byte;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

template <typename T>
constexpr std::string_view TypeName() noexcept
{
#if defined(_MSC_VER)
    return __FUNCSIG__;
#elif defined(__clang__) || defined(__GNUC__)
    return __PRETTY_FUNCTION__;
#else
#error Concord event type IDs require MSVC, Clang, or GCC-compatible signatures.
#endif
}

template <typename T>
constexpr EventTypeDescriptor TypeDescriptor() noexcept
{
    using Event = std::remove_cv_t<std::remove_reference_t<T>>;
    constexpr std::string_view name = TypeName<Event>();
    return {
        .id = {
            HashTypeName(name, 0x9e3779b97f4a7c15ULL),
            HashTypeName(name, 0xcbf29ce484222325ULL),
        },
        .name = name.data(),
        .nameLength = name.size(),
    };
}

} // namespace EventDetail

/** @brief Returns the deterministic runtime identity used for event type `T`. */
template <typename T>
constexpr EventTypeId EventTypeOf() noexcept
{
    return EventDetail::TypeDescriptor<T>().id;
}

} // namespace Concord

#endif // CONCORD_EVENTTYPEID_H
