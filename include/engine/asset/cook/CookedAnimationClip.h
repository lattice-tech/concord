#ifndef CONCORD_COOKEDANIMATIONCLIP_H
#define CONCORD_COOKEDANIMATIONCLIP_H

#include "engine/animation/clip/SkeletalClip.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace Concord::Asset {

/** Resource ceilings enforced when decoding an untrusted cooked animation clip. */
struct CookedAnimationClipLimits {
    std::uint32_t maxNameBytes = 1024u;
    std::uint32_t maxTracks = 65'536u;
    std::uint32_t maxKeysPerChannel = 1'000'000u;
};

/**
 * @brief Versioned little-endian binary format for a cooked skeletal clip.
 *
 * The runtime-ready form the cooker writes and the loader reads, built on the
 * shared Concord::Serialization codec. Encoding is deterministic so re-cooking
 * an identical SkeletalClip yields byte-identical output. The clip name, length,
 * and every bone track's position/rotation/scale keyframes (time, value, easing)
 * are stored.
 *
 * Decoding validates track and keyframe counts against the limits and the
 * easing enum range before trusting the data, so a corrupt or hostile blob is
 * rejected rather than allocated or cast into an invalid value.
 */
namespace CookedAnimationClip {

/** Encodes `clip` to the deterministic cooked byte form. */
std::vector<std::uint8_t> Encode(const Animation::SkeletalClip& clip);

/**
 * Decodes a cooked clip blob. Returns nullopt on bad magic/version, truncation,
 * trailing bytes, a track or key count above `limits`, an over-long name, or an
 * out-of-range easing value.
 */
std::optional<Animation::SkeletalClip> Decode(const std::uint8_t* data,
                                              std::size_t size,
                                              const CookedAnimationClipLimits& limits = {});

} // namespace CookedAnimationClip

} // namespace Concord::Asset

#endif // CONCORD_COOKEDANIMATIONCLIP_H
