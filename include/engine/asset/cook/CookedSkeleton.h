#ifndef CONCORD_COOKEDSKELETON_H
#define CONCORD_COOKEDSKELETON_H

#include "engine/animation/Skeleton.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace Concord::Asset {

/** Resource ceilings enforced when decoding an untrusted cooked skeleton. */
struct CookedSkeletonLimits {
    std::uint32_t maxBones = 65'536u;
    std::uint32_t maxBoneNameBytes = 1024u;
};

/**
 * @brief Versioned little-endian binary format for a cooked skeleton.
 *
 * The runtime-ready form the cooker writes and the loader reads, built on the
 * shared Concord::Serialization codec. Encoding is deterministic so re-cooking
 * an identical Skeleton yields byte-identical output. Every bone's name, parent
 * index, inverse-bind matrix, and bind-local transform is stored, plus the
 * skeleton root pre-transform.
 *
 * Decoding validates the bone count against the limits and every parent index
 * against the bone count before trusting the data, so a corrupt or hostile blob
 * (out-of-range parent, over-long name, truncation) is rejected.
 */
namespace CookedSkeleton {

/** Encodes `skeleton` to the deterministic cooked byte form. */
std::vector<std::uint8_t> Encode(const Animation::Skeleton& skeleton);

/**
 * Decodes a cooked skeleton blob. Returns nullopt on bad magic/version,
 * truncation, trailing bytes, a bone count above `limits`, an over-long bone
 * name, or a parent index that is neither -1 nor a valid bone.
 */
std::optional<Animation::Skeleton> Decode(const std::uint8_t* data,
                                          std::size_t size,
                                          const CookedSkeletonLimits& limits = {});

} // namespace CookedSkeleton

} // namespace Concord::Asset

#endif // CONCORD_COOKEDSKELETON_H
