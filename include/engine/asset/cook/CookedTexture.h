#ifndef CONCORD_COOKEDTEXTURE_H
#define CONCORD_COOKEDTEXTURE_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace Concord::Asset {

/**
 * @brief Engine-owned pixel format of a cooked texture.
 *
 * Deliberately independent of any bgfx/bimg enum (AGENTS.md §5): cooked files
 * stay stable regardless of upstream renumbering, and the render backend maps
 * these to its own format at upload time. Values are fixed and never reordered.
 */
enum class TextureFormat : std::uint8_t {
    Unknown = 0,
    R8 = 1,       ///< single 8-bit channel (masks, height, roughness)
    Rg8 = 2,      ///< two 8-bit channels
    Rgba8 = 3,    ///< four 8-bit channels, linear
    Rgba8Srgb = 4, ///< four 8-bit channels, sRGB-encoded color
    Bc1 = 5,      ///< compressed RGB (DXT1), 8 bytes / 4x4 block
    Bc3 = 6,      ///< compressed RGBA (DXT5), 16 bytes / 4x4 block
    Bc5 = 7,      ///< compressed two-channel (normals), 16 bytes / 4x4 block
    Bc7 = 8,      ///< compressed RGBA high quality, 16 bytes / 4x4 block
};

/** Bytes one mip level of `format` occupies at `width` x `height`. Zero on Unknown. */
std::size_t TextureFormatMipBytes(TextureFormat format, std::uint32_t width,
                                  std::uint32_t height) noexcept;

/**
 * @brief Decoded, ready-to-upload texture: dimensions, format, and mip chain.
 *
 * This is the CPU-side image the cooker produces (already decoded from
 * PNG/JPG/... at cook time) and the runtime uploads directly, skipping any
 * runtime image decode. Each entry of `mips` is one level's raw bytes, level 0
 * first; a texture with no generated chain simply carries a single level.
 */
struct CookedTextureData {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TextureFormat format = TextureFormat::Unknown;
    std::vector<std::vector<std::uint8_t>> mips;
};

/** Resource ceilings enforced when decoding an untrusted cooked texture. */
struct CookedTextureLimits {
    std::uint32_t maxDimension = 16'384u;
    std::uint32_t maxMips = 16u;
    std::size_t maxTotalBytes = 256u * 1024u * 1024u;
};

/**
 * @brief Versioned little-endian binary format for a cooked texture.
 *
 * The runtime-ready form the cooker writes and the loader reads, built on the
 * shared Concord::Serialization codec. Encoding is deterministic so re-cooking
 * identical pixels yields byte-identical output (a prerequisite for content
 * hashing and incremental cook). The layout is a fixed-endianness runtime
 * cache, not a portable interchange format.
 *
 * Decoding validates dimensions, format, mip count, and — crucially — that each
 * stored mip's byte length exactly equals the size its format and dimensions
 * imply, before trusting the data, so a corrupt or hostile blob is rejected
 * rather than uploaded.
 */
namespace CookedTexture {

/** Encodes `texture` to the deterministic cooked byte form. */
std::vector<std::uint8_t> Encode(const CookedTextureData& texture);

/**
 * Decodes a cooked texture blob. Returns nullopt on bad magic/version,
 * truncation, trailing bytes, an unknown format, a zero/oversized dimension, a
 * mip count above `limits`, a per-mip length that disagrees with its computed
 * size, or a total above the byte budget.
 */
std::optional<CookedTextureData> Decode(const std::uint8_t* data, std::size_t size,
                                        const CookedTextureLimits& limits = {});

} // namespace CookedTexture

} // namespace Concord::Asset

#endif // CONCORD_COOKEDTEXTURE_H
