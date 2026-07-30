#ifndef CONCORD_CHECKEDSIZE_H
#define CONCORD_CHECKEDSIZE_H

#include <cstddef>
#include <limits>
#include <type_traits>

namespace Concord::Asset {

constexpr bool TryAddSize(std::size_t lhs, std::size_t rhs,
                          std::size_t& result) noexcept
{
    if (rhs > std::numeric_limits<std::size_t>::max() - lhs) {
        return false;
    }
    result = lhs + rhs;
    return true;
}

constexpr bool TryMultiplySize(std::size_t lhs, std::size_t rhs,
                               std::size_t& result) noexcept
{
    if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
        return false;
    }
    result = lhs * rhs;
    return true;
}

constexpr bool TryRangeEnd(std::size_t offset, std::size_t length,
                           std::size_t containerSize,
                           std::size_t& end) noexcept
{
    return TryAddSize(offset, length, end) && end <= containerSize;
}

template <typename To, typename From>
constexpr bool TryCastSize(From value, To& result) noexcept
{
    static_assert(std::is_integral_v<To> && std::is_integral_v<From>);
    if constexpr (std::is_signed_v<From>) {
        if (value < 0) {
            return false;
        }
    }
    using UnsignedFrom = std::make_unsigned_t<From>;
    const auto unsignedValue = static_cast<UnsignedFrom>(value);
    if (unsignedValue > static_cast<UnsignedFrom>(
            std::numeric_limits<To>::max())) {
        return false;
    }
    result = static_cast<To>(unsignedValue);
    return true;
}

} // namespace Concord::Asset

#endif // CONCORD_CHECKEDSIZE_H
