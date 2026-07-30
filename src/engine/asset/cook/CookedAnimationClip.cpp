#include "engine/asset/cook/CookedAnimationClip.h"

#include "engine/asset/cook/CookedMath.h"
#include "engine/motion/Easing.h"
#include "engine/serialization/BinaryReader.h"
#include "engine/serialization/BinaryWriter.h"

namespace Concord::Asset::CookedAnimationClip {

namespace {

using Serialization::BinaryReader;
using Serialization::BinaryWriter;

constexpr std::uint32_t kMagic = 0x4d494e41u; // 'ANIM' in file order
constexpr std::uint32_t kVersion = 1;

// OutBounce is the last easing curve; a byte above it is rejected on decode.
constexpr std::uint8_t kMaxEasing =
    static_cast<std::uint8_t>(Motion::Easing::OutBounce);

template <typename T, typename PutValue>
void WriteChannel(BinaryWriter& writer, const Animation::AnimationTrack<T>& track,
                  PutValue putValue)
{
    const std::vector<Animation::Keyframe<T>>& keys = track.Keys();
    writer.PutU32(static_cast<std::uint32_t>(keys.size()));
    for (const Animation::Keyframe<T>& key : keys) {
        writer.PutF32(key.time);
        putValue(writer, key.value);
        writer.PutU8(static_cast<std::uint8_t>(key.ease));
    }
}

template <typename T, typename GetValue>
bool ReadChannel(BinaryReader& reader, Animation::AnimationTrack<T>& track,
                 std::uint32_t maxKeys, GetValue getValue)
{
    const std::uint32_t count = reader.GetU32();
    if (!reader.Ok() || count > maxKeys) {
        return false;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        const float time = reader.GetF32();
        const T value = getValue(reader);
        const std::uint8_t ease = reader.GetU8();
        if (!reader.Ok() || ease > kMaxEasing) {
            return false;
        }
        track.AddKey(time, value, static_cast<Motion::Easing>(ease));
    }
    return true;
}

} // namespace

std::vector<std::uint8_t> Encode(const Animation::SkeletalClip& clip)
{
    BinaryWriter writer;
    writer.PutU32(kMagic);
    writer.PutU32(kVersion);
    writer.PutString(clip.name);
    writer.PutF32(clip.length);
    writer.PutU32(static_cast<std::uint32_t>(clip.tracks.size()));
    for (const Animation::BoneTrack& track : clip.tracks) {
        writer.PutI32(track.boneIndex);
        WriteChannel(writer, track.position, PutVec3);
        WriteChannel(writer, track.rotation, PutQuat);
        WriteChannel(writer, track.scale, PutVec3);
    }
    return writer.Take();
}

std::optional<Animation::SkeletalClip> Decode(const std::uint8_t* data,
                                              std::size_t size,
                                              const CookedAnimationClipLimits& limits)
{
    BinaryReader reader(data, size);
    if (reader.GetU32() != kMagic || reader.GetU32() != kVersion) {
        return std::nullopt;
    }

    Animation::SkeletalClip clip;
    clip.name = reader.GetString(limits.maxNameBytes);
    clip.length = reader.GetF32();
    const std::uint32_t trackCount = reader.GetU32();
    if (!reader.Ok() || trackCount > limits.maxTracks) {
        return std::nullopt;
    }

    clip.tracks.resize(trackCount);
    for (Animation::BoneTrack& track : clip.tracks) {
        track.boneIndex = reader.GetI32();
        if (!reader.Ok()
            || !ReadChannel(reader, track.position, limits.maxKeysPerChannel, GetVec3)
            || !ReadChannel(reader, track.rotation, limits.maxKeysPerChannel, GetQuat)
            || !ReadChannel(reader, track.scale, limits.maxKeysPerChannel, GetVec3)) {
            return std::nullopt;
        }
    }

    // A well-formed blob is consumed exactly; trailing bytes mean corruption.
    if (!reader.AtEnd()) {
        return std::nullopt;
    }
    return clip;
}

} // namespace Concord::Asset::CookedAnimationClip
