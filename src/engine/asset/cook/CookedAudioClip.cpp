#include "engine/asset/cook/CookedAudioClip.h"

#include "engine/serialization/BinaryReader.h"
#include "engine/serialization/BinaryWriter.h"

#include <bit>

namespace Concord::Asset {

namespace {

using Serialization::BinaryReader;
using Serialization::BinaryWriter;

constexpr std::uint32_t kMagic = 0x44554143u; // 'CAUD' in file order
constexpr std::uint32_t kVersion = 1;

} // namespace

namespace CookedAudioClip {

std::vector<std::uint8_t> Encode(const CookedAudioClipData& clip)
{
    BinaryWriter writer;
    writer.PutU32(kMagic);
    writer.PutU32(kVersion);
    writer.PutU32(clip.sampleRate);
    writer.PutU16(clip.channels);
    writer.PutU32(static_cast<std::uint32_t>(clip.samples.size()));
    for (const std::int16_t sample : clip.samples) {
        writer.PutU16(std::bit_cast<std::uint16_t>(sample));
    }
    return writer.Take();
}

std::optional<CookedAudioClipData> Decode(const std::uint8_t* data, std::size_t size,
                                          const CookedAudioClipLimits& limits)
{
    BinaryReader reader(data, size);
    if (reader.GetU32() != kMagic || reader.GetU32() != kVersion) {
        return std::nullopt;
    }

    CookedAudioClipData clip;
    clip.sampleRate = reader.GetU32();
    clip.channels = reader.GetU16();
    const std::uint32_t sampleCount = reader.GetU32();
    if (!reader.Ok()
        || clip.sampleRate < limits.minSampleRate
        || clip.sampleRate > limits.maxSampleRate
        || clip.channels == 0 || clip.channels > limits.maxChannels
        || sampleCount > limits.maxSamples
        // Interleaved PCM must contain whole frames across every channel.
        || sampleCount % clip.channels != 0) {
        return std::nullopt;
    }

    clip.samples.reserve(sampleCount);
    for (std::uint32_t i = 0; i < sampleCount; ++i) {
        clip.samples.push_back(std::bit_cast<std::int16_t>(reader.GetU16()));
    }

    // A well-formed blob is consumed exactly; trailing bytes mean corruption.
    if (!reader.Ok() || !reader.AtEnd()) {
        return std::nullopt;
    }
    return clip;
}

} // namespace CookedAudioClip

} // namespace Concord::Asset
