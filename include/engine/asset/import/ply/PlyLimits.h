#ifndef CONCORD_PLY_LIMITS_H
#define CONCORD_PLY_LIMITS_H

#include <cstddef>
#include <cstdint>

namespace Concord::Asset::Ply::Limits {

/** Per-import ceilings; maximum vertex attributes and indices hold 224 MB of logical data. */
inline constexpr std::uint32_t MaxVertexCount = 4'000'000;
inline constexpr std::uint32_t MaxFaceCount = 4'000'000;
inline constexpr std::uint32_t MaxFaceListCount = 65'536;
inline constexpr std::size_t MaxIndexCount = 24'000'000;
inline constexpr std::size_t MaxElementCount = 64;
inline constexpr std::size_t MaxPropertyCount = 64;

} // namespace Concord::Asset::Ply::Limits

#endif // CONCORD_PLY_LIMITS_H
