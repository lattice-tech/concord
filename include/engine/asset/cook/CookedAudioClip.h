#ifndef CONCORD_COOKEDAUDIOCLIP_H
#define CONCORD_COOKEDAUDIOCLIP_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace Concord::Asset {

/**
 * @brief Decoded, ready-to-mix audio clip: format description plus interleaved
 * 16-bit PCM samples.
 *
 * This is the CPU-side clip the cooker produces (already decoded from
 * WAV/OGG/... at cook time) and the runtime feeds to a voice, skipping any
 * runtime audio decode. Samples are signed 16-bit, interleaved by channel
 * (L,R,L,R,... for stereo), so `samples.size()` is always a whole multiple of
 * `channels`.
 */
struct CookedAudioClipData {
    std::uint32_t sampleRate = 0;
    std::uint16_t channels = 0;
    std::vector<std::int16_t> samples;

    /** Frames (samples per channel); 0 when there is no audio. */
    std::size_t FrameCount() const noexcept
    {
        return channels == 0 ? 0 : samples.size() / channels;
    }
};

/** Resource ceilings enforced when decoding an untrusted cooked audio clip. */
struct CookedAudioClipLimits {
    std::uint32_t minSampleRate = 1u;
    std::uint32_t maxSampleRate = 768'000u;
    std::uint16_t maxChannels = 8u;
    std::uint32_t maxSamples = 512u * 1024u * 1024u; ///< ~1 GiB of 16-bit PCM
};

/**
 * @brief Versioned little-endian binary format for a cooked audio clip.
 *
 * The runtime-ready form the cooker writes and the loader reads, built on the
 * shared Concord::Serialization codec. Encoding is deterministic so re-cooking
 * identical PCM yields byte-identical output (a prerequisite for content
 * hashing and incremental cook). The layout is a fixed-endianness runtime
 * cache, not a portable interchange format.
 *
 * Decoding validates the sample rate, channel count, and that the sample count
 * is a whole multiple of the channel count and within the byte budget before
 * trusting the data, so a corrupt or hostile blob is rejected.
 */
namespace CookedAudioClip {

/** Encodes `clip` to the deterministic cooked byte form. */
std::vector<std::uint8_t> Encode(const CookedAudioClipData& clip);

/**
 * Decodes a cooked audio clip blob. Returns nullopt on bad magic/version,
 * truncation, trailing bytes, a sample rate or channel count outside `limits`,
 * a sample count that is not a whole multiple of the channel count, or a count
 * above the budget.
 */
std::optional<CookedAudioClipData> Decode(const std::uint8_t* data, std::size_t size,
                                          const CookedAudioClipLimits& limits = {});

} // namespace CookedAudioClip

} // namespace Concord::Asset

#endif // CONCORD_COOKEDAUDIOCLIP_H
