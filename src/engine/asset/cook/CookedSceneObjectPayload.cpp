#include "engine/asset/cook/CookedSceneCodec.h"

#include "engine/asset/cook/CookedMath.h"

#include <cmath>

namespace Concord::Asset::Detail::CookedSceneCodec {

namespace {

using Serialization::BinaryReader;
using Serialization::BinaryWriter;

bool Finite(float value) noexcept { return std::isfinite(value); }
bool NonNegative(float value) noexcept { return Finite(value) && value >= 0.0f; }

} // namespace

bool ValidateLightPayload(const CookedLightPayload& value) noexcept
{
    const float directionLength = value.direction.x * value.direction.x
        + value.direction.y * value.direction.y
        + value.direction.z * value.direction.z;
    return value.type <= LightType::Spot && IsFinite(value.direction)
        && directionLength > 1.0e-12f && NonNegative(value.intensity)
        && Finite(value.range) && value.range > 0.0f
        && NonNegative(value.sourceRadius)
        && NonNegative(value.directionalAngularRadiusDegrees)
        && value.directionalAngularRadiusDegrees <= 45.0f
        && NonNegative(value.innerAngleDegrees)
        && Finite(value.outerAngleDegrees)
        && value.outerAngleDegrees >= value.innerAngleDegrees
        && value.outerAngleDegrees < 90.0f;
}

bool ValidateSunLightPayload(const CookedSunLightPayload& value) noexcept
{
    return value.timeMode <= Object::SunTimeMode::Manual
        && Finite(value.localSolarTimeHours) && Finite(value.civilTimeHours)
        && Finite(value.latitudeDegrees) && value.latitudeDegrees >= -90.0f
        && value.latitudeDegrees <= 90.0f && Finite(value.longitudeDegrees)
        && value.longitudeDegrees >= -180.0f && value.longitudeDegrees <= 180.0f
        && Finite(value.timeZoneHours) && value.timeZoneHours >= -24.0f
        && value.timeZoneHours <= 24.0f && value.dayOfYear >= 1
        && value.dayOfYear <= 365 && value.year >= 1 && value.year <= 9999
        && value.month >= 1 && value.month <= 12 && value.day >= 1
        && value.day <= 31 && Finite(value.manualElevationDegrees)
        && Finite(value.manualAzimuthDegrees) && Finite(value.northYawDegrees)
        && NonNegative(value.maximumIntensity) && Finite(value.turbidity)
        && value.turbidity >= 1.0f && value.turbidity <= 20.0f
        && NonNegative(value.overrideIntensity)
        && NonNegative(value.directionalAngularRadiusDegrees)
        && value.directionalAngularRadiusDegrees <= 45.0f
        && NonNegative(value.visibleDiskIntensity);
}

bool ValidateCameraPayload(const CookedCameraPayload& value) noexcept
{
    const float upLength = value.up.x * value.up.x + value.up.y * value.up.y
        + value.up.z * value.up.z;
    return IsFinite(value.up) && upLength > 1.0e-12f
        && value.projection <= Projection::Orthographic
        && Finite(value.fovYDegrees) && value.fovYDegrees > 0.0f
        && value.fovYDegrees < 180.0f && Finite(value.orthoHeight)
        && value.orthoHeight > 0.0f && Finite(value.nearPlane)
        && value.nearPlane > 0.0f && Finite(value.farPlane)
        && value.farPlane > value.nearPlane && Finite(value.yaw)
        && Finite(value.pitch) && value.pitch >= -90.0f && value.pitch <= 90.0f;
}

bool ValidateColliderPayload(const CookedColliderPayload& value) noexcept
{
    if (value.shape.type > Collision::ShapeType::Sphere
        || !IsFinite(value.shape.offset)) return false;
    if (value.shape.type == Collision::ShapeType::Box) {
        return IsFinite(value.shape.halfExtents)
            && value.shape.halfExtents.x > 0.0f
            && value.shape.halfExtents.y > 0.0f
            && value.shape.halfExtents.z > 0.0f;
    }
    return Finite(value.shape.radius) && value.shape.radius > 0.0f;
}

void WriteLightPayload(BinaryWriter& writer, const CookedLightPayload& value)
{
    writer.PutU8(static_cast<std::uint8_t>(value.type)); PutVec3(writer, value.direction);
    writer.PutU32(value.color); writer.PutF32(value.intensity); writer.PutF32(value.range);
    writer.PutF32(value.sourceRadius); writer.PutF32(value.directionalAngularRadiusDegrees);
    writer.PutF32(value.innerAngleDegrees); writer.PutF32(value.outerAngleDegrees);
    PutBool(writer, value.castShadow);
}

CookedLightPayload ReadLightPayload(BinaryReader& reader)
{
    CookedLightPayload value;
    value.type = static_cast<LightType>(reader.GetU8()); value.direction = GetVec3(reader);
    value.color = reader.GetU32(); value.intensity = reader.GetF32();
    value.range = reader.GetF32(); value.sourceRadius = reader.GetF32();
    value.directionalAngularRadiusDegrees = reader.GetF32();
    value.innerAngleDegrees = reader.GetF32(); value.outerAngleDegrees = reader.GetF32();
    GetBool(reader, value.castShadow); return value;
}

void WriteSunLightPayload(BinaryWriter& writer, const CookedSunLightPayload& value)
{
    writer.PutU8(static_cast<std::uint8_t>(value.timeMode));
    writer.PutF32(value.localSolarTimeHours); writer.PutF32(value.civilTimeHours);
    writer.PutF32(value.latitudeDegrees); writer.PutF32(value.longitudeDegrees);
    writer.PutF32(value.timeZoneHours); writer.PutI32(value.dayOfYear);
    writer.PutI32(value.year); writer.PutI32(value.month); writer.PutI32(value.day);
    writer.PutF32(value.manualElevationDegrees); writer.PutF32(value.manualAzimuthDegrees);
    writer.PutF32(value.northYawDegrees); writer.PutF32(value.maximumIntensity);
    writer.PutF32(value.turbidity); PutBool(writer, value.overrideColorEnabled);
    writer.PutU32(value.overrideColor); PutBool(writer, value.overrideIntensityEnabled);
    writer.PutF32(value.overrideIntensity);
    writer.PutF32(value.directionalAngularRadiusDegrees); PutBool(writer, value.visibleDisk);
    writer.PutF32(value.visibleDiskIntensity); PutBool(writer, value.castShadow);
}

CookedSunLightPayload ReadSunLightPayload(BinaryReader& reader)
{
    CookedSunLightPayload value;
    value.timeMode = static_cast<Object::SunTimeMode>(reader.GetU8());
    value.localSolarTimeHours = reader.GetF32(); value.civilTimeHours = reader.GetF32();
    value.latitudeDegrees = reader.GetF32(); value.longitudeDegrees = reader.GetF32();
    value.timeZoneHours = reader.GetF32(); value.dayOfYear = reader.GetI32();
    value.year = reader.GetI32(); value.month = reader.GetI32(); value.day = reader.GetI32();
    value.manualElevationDegrees = reader.GetF32();
    value.manualAzimuthDegrees = reader.GetF32(); value.northYawDegrees = reader.GetF32();
    value.maximumIntensity = reader.GetF32(); value.turbidity = reader.GetF32();
    GetBool(reader, value.overrideColorEnabled); value.overrideColor = reader.GetU32();
    GetBool(reader, value.overrideIntensityEnabled); value.overrideIntensity = reader.GetF32();
    value.directionalAngularRadiusDegrees = reader.GetF32();
    GetBool(reader, value.visibleDisk); value.visibleDiskIntensity = reader.GetF32();
    GetBool(reader, value.castShadow); return value;
}

} // namespace Concord::Asset::Detail::CookedSceneCodec
