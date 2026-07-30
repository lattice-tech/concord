#include "engine/asset/cook/CookedSceneCodec.h"

#include <bit>
#include <cmath>

namespace Concord::Asset::Detail::CookedSceneCodec {

CookedNodeKind Kind(const CookedNodePayload& payload) noexcept
{
    return static_cast<CookedNodeKind>(payload.index());
}

bool IsFinite(const Vector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

bool IsValidTransform(const Transform& transform) noexcept
{
    const Quaternion& rotation = transform.rotation;
    const float lengthSquared = rotation.x * rotation.x + rotation.y * rotation.y
        + rotation.z * rotation.z + rotation.w * rotation.w;
    return IsFinite(transform.position) && IsFinite(transform.scale)
        && std::isfinite(rotation.x) && std::isfinite(rotation.y)
        && std::isfinite(rotation.z) && std::isfinite(rotation.w)
        && std::isfinite(lengthSquared) && lengthSquared > 1.0e-12f;
}

void PutBool(Serialization::BinaryWriter& writer, bool value)
{
    writer.PutU8(value ? 1u : 0u);
}

bool GetBool(Serialization::BinaryReader& reader, bool& value)
{
    const std::uint8_t encoded = reader.GetU8();
    if (!reader.Ok() || encoded > 1u) {
        reader.Fail();
        return false;
    }
    value = encoded != 0u;
    return true;
}

void PutF64(Serialization::BinaryWriter& writer, double value)
{
    writer.PutU64(std::bit_cast<std::uint64_t>(value));
}

double GetF64(Serialization::BinaryReader& reader)
{
    return std::bit_cast<double>(reader.GetU64());
}

void PutAssetId(Serialization::BinaryWriter& writer, const AssetId& id)
{
    writer.PutString(id.IsValid() ? id.Key() : std::string_view{});
}

bool GetAssetId(Serialization::BinaryReader& reader, AssetType expected,
                bool required, std::uint32_t maxKeyBytes, AssetId& id)
{
    const std::string key = reader.GetString(maxKeyBytes);
    if (!reader.Ok()) {
        return false;
    }
    if (key.empty()) {
        id = {};
        return !required;
    }
    const std::optional<AssetId> parsed = AssetId::FromKey(key);
    if (!parsed || parsed->Type() != expected) {
        reader.Fail();
        return false;
    }
    id = *parsed;
    return true;
}

} // namespace Concord::Asset::Detail::CookedSceneCodec
