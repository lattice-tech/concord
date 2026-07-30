#include "engine/asset/cook/CookedSkeleton.h"

#include "engine/asset/cook/CookedMath.h"
#include "engine/serialization/BinaryReader.h"
#include "engine/serialization/BinaryWriter.h"

namespace Concord::Asset::CookedSkeleton {

namespace {

using Serialization::BinaryReader;
using Serialization::BinaryWriter;

constexpr std::uint32_t kMagic = 0x4c454b53u; // 'SKEL' in file order
constexpr std::uint32_t kVersion = 1;

} // namespace

std::vector<std::uint8_t> Encode(const Animation::Skeleton& skeleton)
{
    BinaryWriter writer;
    writer.PutU32(kMagic);
    writer.PutU32(kVersion);
    PutMatrix4(writer, skeleton.rootTransform);
    writer.PutU32(static_cast<std::uint32_t>(skeleton.bones.size()));
    for (const Animation::Bone& bone : skeleton.bones) {
        writer.PutString(bone.name);
        writer.PutI32(bone.parent);
        PutMatrix4(writer, bone.inverseBind);
        PutTransform(writer, bone.bindLocal);
    }
    return writer.Take();
}

std::optional<Animation::Skeleton> Decode(const std::uint8_t* data,
                                          std::size_t size,
                                          const CookedSkeletonLimits& limits)
{
    BinaryReader reader(data, size);
    if (reader.GetU32() != kMagic || reader.GetU32() != kVersion) {
        return std::nullopt;
    }

    Animation::Skeleton skeleton;
    skeleton.rootTransform = GetMatrix4(reader);
    const std::uint32_t boneCount = reader.GetU32();
    if (!reader.Ok() || boneCount > limits.maxBones) {
        return std::nullopt;
    }

    skeleton.bones.resize(boneCount);
    for (Animation::Bone& bone : skeleton.bones) {
        bone.name = reader.GetString(limits.maxBoneNameBytes);
        const std::int32_t parent = reader.GetI32();
        // A parent is either -1 (root) or a valid index into the bone array.
        if (parent < -1 || parent >= static_cast<std::int32_t>(boneCount)) {
            return std::nullopt;
        }
        bone.parent = parent;
        bone.inverseBind = GetMatrix4(reader);
        bone.bindLocal = GetTransform(reader);
    }

    // A well-formed blob is consumed exactly; trailing bytes mean corruption.
    if (!reader.Ok() || !reader.AtEnd()) {
        return std::nullopt;
    }
    return skeleton;
}

} // namespace Concord::Asset::CookedSkeleton
