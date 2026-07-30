#include "engine/asset/id/AssetContentHash.h"

#include <array>
#include <cstddef>

namespace Concord::Asset {
namespace {

constexpr std::uint64_t kFnvPrime = 0x100000001b3ull;

void MixByte(std::uint64_t& lane, std::uint8_t byte) noexcept
{
    lane ^= static_cast<std::uint64_t>(byte);
    lane *= kFnvPrime;
}

} // namespace

void AssetContentHasher::Mix(const void* data, std::size_t size) noexcept
{
    // Length-delimit every chunk so concatenated fields cannot collide with a
    // single field carrying their catenation.
    MixU64(static_cast<std::uint64_t>(size));
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        MixByte(m_high, bytes[i]);
        // The low lane consumes bytes in reverse to decorrelate the two lanes.
        MixByte(m_low, bytes[size - 1u - i]);
    }
}

void AssetContentHasher::Mix(std::string_view text) noexcept
{
    Mix(text.data(), text.size());
}

void AssetContentHasher::MixU64(std::uint64_t value) noexcept
{
    for (unsigned shift = 56; shift <= 56; shift -= 8) {
        const auto byte = static_cast<std::uint8_t>(value >> shift);
        MixByte(m_high, byte);
        MixByte(m_low, byte);
        if (shift == 0) {
            break;
        }
    }
}

AssetContentHash AssetContentHasher::Finish() const noexcept
{
    AssetContentHash hash{m_high, m_low};
    // The zero value is reserved for "no hash"; the seeded lanes make an all
    // zero result impossible for any input, but guard defensively anyway.
    if (!hash.IsValid()) {
        hash.low = 1u;
    }
    return hash;
}

std::string AssetContentHash::ToHex() const
{
    constexpr std::array<char, 16> digits{'0', '1', '2', '3', '4', '5', '6', '7',
                                          '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string out(32, '0');
    const std::uint64_t lanes[2] = {high, low};
    for (int lane = 0; lane < 2; ++lane) {
        for (int nibble = 0; nibble < 16; ++nibble) {
            const auto shift = static_cast<unsigned>((15 - nibble) * 4);
            const auto value = static_cast<std::size_t>((lanes[lane] >> shift) & 0xfull);
            out[static_cast<std::size_t>(lane) * 16u + static_cast<std::size_t>(nibble)] =
                digits[value];
        }
    }
    return out;
}

AssetContentHash HashBytes(const void* data, std::size_t size) noexcept
{
    AssetContentHasher hasher;
    hasher.Mix(data, size);
    return hasher.Finish();
}

} // namespace Concord::Asset
