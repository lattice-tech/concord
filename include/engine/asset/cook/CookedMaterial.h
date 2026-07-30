#ifndef CONCORD_COOKEDMATERIAL_H
#define CONCORD_COOKEDMATERIAL_H

#include "engine/material/MaterialDesc.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace Concord::Asset {

/** Resource ceiling enforced when decoding an untrusted cooked material. */
struct CookedMaterialLimits {
    std::uint32_t maxTexturePathBytes = 4096u;
};

/**
 * @brief Versioned little-endian binary format for a cooked material.
 *
 * The runtime-ready form the cooker writes and the loader reads, built on the
 * shared Concord::Serialization codec. Encoding is deterministic so re-cooking
 * an identical MaterialDesc yields byte-identical output (a prerequisite for
 * content hashing and incremental cook). The layout is a fixed-endianness
 * runtime cache, not a portable interchange format.
 *
 * Every scalar, color, and the four texture reference paths are stored; enum
 * fields are written as their canonical small integer and re-validated on
 * decode, so an out-of-range enum or an over-long path is rejected rather than
 * cast into an invalid value.
 */
namespace CookedMaterial {

/** Encodes `material` to the deterministic cooked byte form. */
std::vector<std::uint8_t> Encode(const Material::MaterialDesc& material);

/**
 * Decodes a cooked material blob. Returns nullopt on bad magic/version,
 * truncation, trailing bytes, an out-of-range enum, or a texture path longer
 * than `limits` allows.
 */
std::optional<Material::MaterialDesc> Decode(const std::uint8_t* data,
                                             std::size_t size,
                                             const CookedMaterialLimits& limits = {});

} // namespace CookedMaterial

} // namespace Concord::Asset

#endif // CONCORD_COOKEDMATERIAL_H
