#include "engine/asset/cook/CookedSceneCodec.h"

#include "engine/asset/cook/CookedMath.h"

#include <cmath>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace Concord::Asset::Detail::CookedSceneCodec {

namespace {

using Serialization::BinaryReader;
using Serialization::BinaryWriter;

bool Finite(float value) noexcept { return std::isfinite(value); }
bool AddReference(const AssetId& id, AssetType expected, bool required,
                  const CookedSceneGraphLimits& limits, DecodeBudget& budget,
                  std::size_t& encodedBytes)
{
    encodedBytes += sizeof(std::uint32_t);
    if (!id.IsValid()) return !required;
    if (id.Type() != expected || id.Key().size() > limits.maxAssetKeyBytes
        || budget.assetReferences >= limits.maxAssetReferences) return false;
    ++budget.assetReferences;
    encodedBytes += id.Key().size();
    return true;
}

bool ReadReference(BinaryReader& reader, AssetType expected, bool required,
                   const CookedSceneGraphLimits& limits, AssetId& id)
{
    return GetAssetId(reader, expected, required, limits.maxAssetKeyBytes, id);
}

} // namespace

bool ValidatePayload(const CookedNodeData& data,
                     const CookedSceneGraphLimits& limits,
                     DecodeBudget& budget, std::size_t& encodedBytes)
{
    encodedBytes = 46u;
    if (data.settings.reflectionMode > Object::ReflectionMode::RealtimeScene
        || !Finite(data.settings.reflectivity)
        || data.settings.reflectivity < 0.0f || data.settings.reflectivity > 1.0f
        || !IsValidTransform(data.localTransform)) return false;

    bool valid = std::visit([&](const auto& value) -> bool {
        using T = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<T, CookedPrimitivePayload>) {
            encodedBytes += 13u;
            return value.shape <= Object::PrimitiveShape::Torus
                && IsFinite(value.size) && value.size.x > 0.0f
                && value.size.y > 0.0f && value.size.z > 0.0f
                && AddReference(value.material, AssetType::Material, false,
                                limits, budget, encodedBytes);
        } else if constexpr (std::is_same_v<T, CookedLightPayload>) {
            encodedBytes += 42u; return ValidateLightPayload(value);
        } else if constexpr (std::is_same_v<T, CookedSunLightPayload>) {
            encodedBytes += 78u; return ValidateSunLightPayload(value);
        } else if constexpr (std::is_same_v<T, CookedCameraPayload>) {
            encodedBytes += 37u; return ValidateCameraPayload(value);
        } else if constexpr (std::is_same_v<T, CookedModelPayload>) {
            if (value.parts.empty() || value.parts.size() > limits.maxModelPartsPerNode
                || value.animations.size() > limits.maxAnimationsPerNode) return false;
            encodedBytes += 8u;
            for (const CookedModelPart& part : value.parts) {
                if (!AddReference(part.mesh, AssetType::Mesh, true, limits, budget,
                                  encodedBytes)
                    || !AddReference(part.material, AssetType::Material, false,
                                     limits, budget, encodedBytes)) return false;
            }
            if (!AddReference(value.skeleton, AssetType::Skeleton, false,
                              limits, budget, encodedBytes)) return false;
            std::unordered_set<std::string> animations;
            for (const AssetId& animation : value.animations) {
                if (!AddReference(animation, AssetType::Animation, true,
                                  limits, budget, encodedBytes)
                    || !animations.insert(animation.Key()).second) return false;
            }
            return true;
        } else if constexpr (std::is_same_v<T, CookedColliderPayload>) {
            encodedBytes += 37u; return ValidateColliderPayload(value);
        } else if constexpr (std::is_same_v<T, CookedParticlePayload>) {
            return ValidateParticle(value, limits, budget, encodedBytes);
        } else {
            return AddReference(value.prefab, AssetType::Prefab, true,
                                limits, budget, encodedBytes);
        }
    }, data.payload);
    return valid && encodedBytes + 16u <= limits.maxNodeRecordBytes;
}

void WriteNodeData(BinaryWriter& writer, const CookedNodeData& data)
{
    writer.PutU8(static_cast<std::uint8_t>(Kind(data.payload)));
    writer.PutU8(static_cast<std::uint8_t>(data.settings.reflectionMode));
    writer.PutF32(data.settings.reflectivity); PutTransform(writer, data.localTransform);
    std::visit([&](const auto& value) {
        using T = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<T, CookedPrimitivePayload>) {
            writer.PutU8(static_cast<std::uint8_t>(value.shape)); PutVec3(writer, value.size);
            PutAssetId(writer, value.material);
        } else if constexpr (std::is_same_v<T, CookedLightPayload>) {
            WriteLightPayload(writer, value);
        } else if constexpr (std::is_same_v<T, CookedSunLightPayload>) {
            WriteSunLightPayload(writer, value);
        } else if constexpr (std::is_same_v<T, CookedCameraPayload>) {
            PutVec3(writer, value.up); writer.PutU8(static_cast<std::uint8_t>(value.projection));
            writer.PutF32(value.fovYDegrees); writer.PutF32(value.orthoHeight);
            writer.PutF32(value.nearPlane); writer.PutF32(value.farPlane);
            writer.PutF32(value.yaw); writer.PutF32(value.pitch);
        } else if constexpr (std::is_same_v<T, CookedModelPayload>) {
            writer.PutU32(static_cast<std::uint32_t>(value.parts.size()));
            for (const CookedModelPart& part : value.parts) {
                PutAssetId(writer, part.mesh); PutAssetId(writer, part.material);
            }
            PutAssetId(writer, value.skeleton);
            writer.PutU32(static_cast<std::uint32_t>(value.animations.size()));
            for (const AssetId& animation : value.animations) PutAssetId(writer, animation);
        } else if constexpr (std::is_same_v<T, CookedColliderPayload>) {
            writer.PutU8(static_cast<std::uint8_t>(value.shape.type));
            PutVec3(writer, value.shape.halfExtents); writer.PutF32(value.shape.radius);
            PutVec3(writer, value.shape.offset); writer.PutU32(value.layer); writer.PutU32(value.mask);
        } else if constexpr (std::is_same_v<T, CookedParticlePayload>) {
            WriteParticle(writer, value);
        } else PutAssetId(writer, value.prefab);
    }, data.payload);
}

bool ReadNodeData(BinaryReader& reader, const CookedSceneGraphLimits& limits,
                  DecodeBudget& budget, CookedNodeData& data)
{
    const auto kind = static_cast<CookedNodeKind>(reader.GetU8());
    data.settings.reflectionMode = static_cast<Object::ReflectionMode>(reader.GetU8());
    data.settings.reflectivity = reader.GetF32(); data.localTransform = GetTransform(reader);
    switch (kind) {
        case CookedNodeKind::Primitive: {
            CookedPrimitivePayload value;
            value.shape = static_cast<Object::PrimitiveShape>(reader.GetU8());
            value.size = GetVec3(reader);
            if (!ReadReference(reader, AssetType::Material, false, limits,
                               value.material)) return false;
            data.payload = std::move(value); break;
        }
        case CookedNodeKind::Light: data.payload = ReadLightPayload(reader); break;
        case CookedNodeKind::SunLight: data.payload = ReadSunLightPayload(reader); break;
        case CookedNodeKind::Camera: {
            CookedCameraPayload value;
            value.up = GetVec3(reader);
            value.projection = static_cast<Projection>(reader.GetU8());
            value.fovYDegrees = reader.GetF32(); value.orthoHeight = reader.GetF32();
            value.nearPlane = reader.GetF32(); value.farPlane = reader.GetF32();
            value.yaw = reader.GetF32(); value.pitch = reader.GetF32();
            data.payload = value; break;
        }
        case CookedNodeKind::Model: {
            CookedModelPayload value;
            const std::uint32_t partCount = reader.GetU32();
            if (!reader.Ok() || partCount == 0u || partCount > limits.maxModelPartsPerNode
                || partCount > reader.Remaining() / 8u) return false;
            value.parts.resize(partCount);
            for (CookedModelPart& part : value.parts) {
                if (!ReadReference(reader, AssetType::Mesh, true, limits, part.mesh)
                    || !ReadReference(reader, AssetType::Material, false, limits,
                                      part.material)) return false;
            }
            if (!ReadReference(reader, AssetType::Skeleton, false, limits,
                               value.skeleton)) return false;
            const std::uint32_t animationCount = reader.GetU32();
            if (!reader.Ok() || animationCount > limits.maxAnimationsPerNode
                || animationCount > reader.Remaining() / 4u) return false;
            value.animations.resize(animationCount);
            for (AssetId& animation : value.animations) {
                if (!ReadReference(reader, AssetType::Animation, true, limits,
                                   animation)) return false;
            }
            data.payload = std::move(value); break;
        }
        case CookedNodeKind::Collider: {
            CookedColliderPayload value;
            value.shape.type = static_cast<Collision::ShapeType>(reader.GetU8());
            value.shape.halfExtents = GetVec3(reader); value.shape.radius = reader.GetF32();
            value.shape.offset = GetVec3(reader); value.layer = reader.GetU32();
            value.mask = reader.GetU32(); data.payload = value; break;
        }
        case CookedNodeKind::ParticleEmitter: {
            CookedParticlePayload value;
            if (!ReadParticle(reader, limits, value)) return false;
            data.payload = std::move(value); break;
        }
        case CookedNodeKind::PrefabInstance: {
            CookedPrefabInstancePayload value;
            if (!ReadReference(reader, AssetType::Prefab, true, limits,
                               value.prefab)) return false;
            data.payload = std::move(value); break;
        }
        default: return false;
    }
    if (!reader.Ok()) return false;
    DecodeBudget validationBudget = budget;
    std::size_t encodedBytes = 0;
    if (!ValidatePayload(data, limits, validationBudget, encodedBytes)) return false;
    budget = validationBudget;
    return true;
}

} // namespace Concord::Asset::Detail::CookedSceneCodec
