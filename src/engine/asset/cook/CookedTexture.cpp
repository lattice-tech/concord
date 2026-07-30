#include "engine/asset/cook/CookedTexture.h"

#include "engine/serialization/BinaryReader.h"
#include "engine/serialization/BinaryWriter.h"

namespace Concord::Asset {

namespace {

using Serialization::BinaryReader;
using Serialization::BinaryWriter;

constexpr std::uint32_t kMagic = 0x58455443u; // 'CTEX' in file order
constexpr std::uint32_t kVersion = 1;
constexpr std::uint8_t kMaxFormat = static_cast<std::uint8_t>(TextureFormat::Bc7);

bool IsBlockCompressed(TextureFormat format) noexcept
{
    switch (format) {
        case TextureFormat::Bc1:
        case TextureFormat::Bc3:
        case TextureFormat::Bc5:
        case TextureFormat::Bc7:
            return true;
        default:
            return false;
    }
}

/** Bytes per pixel for an uncompressed format, or 0 for compressed/unknown. */
std::size_t UncompressedBytesPerPixel(TextureFormat format) noexcept
{
    switch (format) {
        case TextureFormat::R8:        return 1;
        case TextureFormat::Rg8:       return 2;
        case TextureFormat::Rgba8:     return 4;
        case TextureFormat::Rgba8Srgb: return 4;
        default:                       return 0;
    }
}

/** Bytes per 4x4 block for a compressed format, or 0 otherwise. */
std::size_t CompressedBlockBytes(TextureFormat format) noexcept
{
    switch (format) {
        case TextureFormat::Bc1: return 8;
        case TextureFormat::Bc3: return 16;
        case TextureFormat::Bc5: return 16;
        case TextureFormat::Bc7: return 16;
        default:                 return 0;
    }
}

} // namespace

std::size_t TextureFormatMipBytes(TextureFormat format, std::uint32_t width,
                                  std::uint32_t height) noexcept
{
    if (format == TextureFormat::Unknown) {
        return 0;
    }
    if (IsBlockCompressed(format)) {
        // Compressed formats round up to whole 4x4 blocks.
        const std::size_t blocksX = (static_cast<std::size_t>(width) + 3u) / 4u;
        const std::size_t blocksY = (static_cast<std::size_t>(height) + 3u) / 4u;
        return blocksX * blocksY * CompressedBlockBytes(format);
    }
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height)
        * UncompressedBytesPerPixel(format);
}

namespace CookedTexture {

std::vector<std::uint8_t> Encode(const CookedTextureData& texture)
{
    BinaryWriter writer;
    writer.PutU32(kMagic);
    writer.PutU32(kVersion);
    writer.PutU32(texture.width);
    writer.PutU32(texture.height);
    writer.PutU8(static_cast<std::uint8_t>(texture.format));
    writer.PutU32(static_cast<std::uint32_t>(texture.mips.size()));
    for (const std::vector<std::uint8_t>& mip : texture.mips) {
        writer.PutBytes(mip.data(), mip.size());
    }
    return writer.Take();
}

std::optional<CookedTextureData> Decode(const std::uint8_t* data, std::size_t size,
                                        const CookedTextureLimits& limits)
{
    BinaryReader reader(data, size);
    if (reader.GetU32() != kMagic || reader.GetU32() != kVersion) {
        return std::nullopt;
    }

    CookedTextureData texture;
    texture.width = reader.GetU32();
    texture.height = reader.GetU32();
    const std::uint8_t format = reader.GetU8();
    const std::uint32_t mipCount = reader.GetU32();
    if (!reader.Ok() || format == 0 || format > kMaxFormat
        || texture.width == 0 || texture.height == 0
        || texture.width > limits.maxDimension || texture.height > limits.maxDimension
        || mipCount == 0 || mipCount > limits.maxMips) {
        return std::nullopt;
    }
    texture.format = static_cast<TextureFormat>(format);

    std::string block;
    std::size_t totalBytes = 0;
    std::uint32_t mipWidth = texture.width;
    std::uint32_t mipHeight = texture.height;
    texture.mips.reserve(mipCount);
    for (std::uint32_t level = 0; level < mipCount; ++level) {
        reader.GetBytes(block, limits.maxTotalBytes);
        // The stored length must exactly match what the format and this level's
        // dimensions imply; anything else is corruption, not a valid mip.
        if (!reader.Ok()
            || block.size() != TextureFormatMipBytes(texture.format, mipWidth, mipHeight)) {
            return std::nullopt;
        }
        totalBytes += block.size();
        if (totalBytes > limits.maxTotalBytes) {
            return std::nullopt;
        }
        texture.mips.emplace_back(block.begin(), block.end());
        mipWidth = mipWidth > 1u ? mipWidth / 2u : 1u;
        mipHeight = mipHeight > 1u ? mipHeight / 2u : 1u;
    }

    // A well-formed blob is consumed exactly; trailing bytes mean corruption.
    if (!reader.AtEnd()) {
        return std::nullopt;
    }
    return texture;
}

} // namespace CookedTexture

} // namespace Concord::Asset
