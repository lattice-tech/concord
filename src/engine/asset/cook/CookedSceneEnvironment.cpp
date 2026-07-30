#include "engine/asset/cook/CookedSceneCodec.h"

#include <cmath>

namespace Concord::Asset::Detail::CookedSceneCodec {

namespace {

using Serialization::BinaryReader;
using Serialization::BinaryWriter;

bool NonNegative(float value) noexcept
{
    return std::isfinite(value) && value >= 0.0f;
}

bool Unit(float value) noexcept
{
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

void WriteSky(BinaryWriter& writer, const SkyEnvironment& sky)
{
    writer.PutU8(static_cast<std::uint8_t>(sky.mode));
    writer.PutU32(sky.solidColor); writer.PutU32(sky.zenithColor);
    writer.PutU32(sky.horizonColor); writer.PutU32(sky.groundColor);
    writer.PutU32(sky.ambientColor); writer.PutF32(sky.intensity);
    writer.PutF32(sky.ambientIntensity); writer.PutF32(sky.nightAmbientIntensity);
    writer.PutF32(sky.horizonFalloff); writer.PutF32(sky.sunDiskIntensity);
    PutBool(writer, sky.sunDisk); PutBool(writer, sky.clouds);
    writer.PutF32(sky.cloudCoverage); writer.PutF32(sky.cloudDensity);
    writer.PutF32(sky.cloudBaseHeight); writer.PutF32(sky.cloudThickness);
    writer.PutF32(sky.cloudOffsetEast); writer.PutF32(sky.cloudOffsetNorth);
    writer.PutF32(sky.cloudScale); writer.PutF32(sky.cloudErosion);
    writer.PutF32(sky.cloudDetail); writer.PutF32(sky.cloudSilverLining);
    writer.PutU32(sky.cloudLitColor); writer.PutU32(sky.cloudShadowColor);
    writer.PutU32(sky.cloudFireColor); writer.PutF32(sky.cloudFireEmission);
    PutBool(writer, sky.volumetricFog); writer.PutF32(sky.fogDensity);
    writer.PutF32(sky.fogBaseHeight); writer.PutF32(sky.fogHeightFalloff);
    writer.PutU32(sky.fogColor);
}

bool ReadSky(BinaryReader& reader, SkyEnvironment& sky)
{
    const std::uint8_t mode = reader.GetU8();
    sky.mode = static_cast<SkyMode>(mode);
    sky.solidColor = reader.GetU32(); sky.zenithColor = reader.GetU32();
    sky.horizonColor = reader.GetU32(); sky.groundColor = reader.GetU32();
    sky.ambientColor = reader.GetU32(); sky.intensity = reader.GetF32();
    sky.ambientIntensity = reader.GetF32();
    sky.nightAmbientIntensity = reader.GetF32();
    sky.horizonFalloff = reader.GetF32(); sky.sunDiskIntensity = reader.GetF32();
    GetBool(reader, sky.sunDisk); GetBool(reader, sky.clouds);
    sky.cloudCoverage = reader.GetF32(); sky.cloudDensity = reader.GetF32();
    sky.cloudBaseHeight = reader.GetF32(); sky.cloudThickness = reader.GetF32();
    sky.cloudOffsetEast = reader.GetF32(); sky.cloudOffsetNorth = reader.GetF32();
    sky.cloudScale = reader.GetF32(); sky.cloudErosion = reader.GetF32();
    sky.cloudDetail = reader.GetF32(); sky.cloudSilverLining = reader.GetF32();
    sky.cloudLitColor = reader.GetU32(); sky.cloudShadowColor = reader.GetU32();
    sky.cloudFireColor = reader.GetU32(); sky.cloudFireEmission = reader.GetF32();
    GetBool(reader, sky.volumetricFog); sky.fogDensity = reader.GetF32();
    sky.fogBaseHeight = reader.GetF32(); sky.fogHeightFalloff = reader.GetF32();
    sky.fogColor = reader.GetU32();
    return reader.Ok() && mode <= static_cast<std::uint8_t>(SkyMode::Procedural);
}

bool ValidateSky(const SkyEnvironment& sky) noexcept
{
    return sky.mode <= SkyMode::Procedural
        && NonNegative(sky.intensity) && NonNegative(sky.ambientIntensity)
        && NonNegative(sky.nightAmbientIntensity)
        && NonNegative(sky.horizonFalloff) && NonNegative(sky.sunDiskIntensity)
        && Unit(sky.cloudCoverage) && NonNegative(sky.cloudDensity)
        && NonNegative(sky.cloudBaseHeight) && sky.cloudThickness > 0.0f
        && std::isfinite(sky.cloudThickness)
        && std::isfinite(sky.cloudOffsetEast)
        && std::isfinite(sky.cloudOffsetNorth) && sky.cloudScale > 0.0f
        && std::isfinite(sky.cloudScale) && Unit(sky.cloudErosion)
        && Unit(sky.cloudDetail) && NonNegative(sky.cloudSilverLining)
        && NonNegative(sky.cloudFireEmission) && NonNegative(sky.fogDensity)
        && std::isfinite(sky.fogBaseHeight)
        && NonNegative(sky.fogHeightFalloff);
}

} // namespace

bool ValidateEnvironment(const EnvironmentSettings& value) noexcept
{
    const auto& time = value.time;
    const auto& clouds = value.clouds;
    const auto& fog = value.heightFog;
    return ValidateSky(value.sky) && std::isfinite(time.timeSeconds)
        && std::isfinite(time.timeScale) && NonNegative(time.fixedStepSeconds)
        && std::isfinite(time.stepRemainderSeconds)
        && time.stepRemainderSeconds >= 0.0
        && clouds.colorMode <= CloudColorMode::Fire && Unit(clouds.coverage)
        && NonNegative(clouds.density) && std::isfinite(clouds.baseAltitudeKm)
        && clouds.thicknessKm > 0.0f && std::isfinite(clouds.thicknessKm)
        && clouds.shapeScaleKm > 0.0f && std::isfinite(clouds.shapeScaleKm)
        && Unit(clouds.erosion) && Unit(clouds.detail)
        && std::isfinite(clouds.windDirectionDegrees)
        && NonNegative(clouds.windSpeedKmPerSecond)
        && NonNegative(clouds.fireEmission) && NonNegative(clouds.silverLining)
        && NonNegative(fog.density) && std::isfinite(fog.baseHeight)
        && NonNegative(fog.heightFalloff) && NonNegative(fog.maxDistance)
        && NonNegative(fog.startDistance) && fog.startDistance <= fog.maxDistance
        && NonNegative(fog.inscatteringIntensity)
        && std::isfinite(fog.directionalAnisotropy)
        && fog.directionalAnisotropy >= -0.99f
        && fog.directionalAnisotropy <= 0.99f;
}

void WriteEnvironment(BinaryWriter& writer, const EnvironmentSettings& value)
{
    WriteSky(writer, value.sky);
    PutF64(writer, value.time.timeSeconds); writer.PutF32(value.time.timeScale);
    writer.PutF32(value.time.fixedStepSeconds);
    PutF64(writer, value.time.stepRemainderSeconds); PutBool(writer, value.time.paused);
    PutBool(writer, value.clouds.enabled);
    writer.PutU8(static_cast<std::uint8_t>(value.clouds.colorMode));
    writer.PutF32(value.clouds.coverage); writer.PutF32(value.clouds.density);
    writer.PutF32(value.clouds.baseAltitudeKm); writer.PutF32(value.clouds.thicknessKm);
    writer.PutF32(value.clouds.shapeScaleKm); writer.PutF32(value.clouds.erosion);
    writer.PutF32(value.clouds.detail); writer.PutF32(value.clouds.windDirectionDegrees);
    writer.PutF32(value.clouds.windSpeedKmPerSecond);
    writer.PutU32(value.clouds.litColor); writer.PutU32(value.clouds.shadowColor);
    writer.PutU32(value.clouds.fireColor); writer.PutF32(value.clouds.fireEmission);
    writer.PutF32(value.clouds.silverLining); PutBool(writer, value.heightFog.enabled);
    writer.PutF32(value.heightFog.density); writer.PutF32(value.heightFog.baseHeight);
    writer.PutF32(value.heightFog.heightFalloff);
    writer.PutF32(value.heightFog.maxDistance); writer.PutF32(value.heightFog.startDistance);
    writer.PutU32(value.heightFog.inscatteringColor);
    writer.PutF32(value.heightFog.inscatteringIntensity);
    writer.PutF32(value.heightFog.directionalAnisotropy);
}

bool ReadEnvironment(BinaryReader& reader, EnvironmentSettings& value)
{
    if (!ReadSky(reader, value.sky)) return false;
    value.time.timeSeconds = GetF64(reader); value.time.timeScale = reader.GetF32();
    value.time.fixedStepSeconds = reader.GetF32();
    value.time.stepRemainderSeconds = GetF64(reader); GetBool(reader, value.time.paused);
    GetBool(reader, value.clouds.enabled);
    const std::uint8_t colorMode = reader.GetU8();
    value.clouds.colorMode = static_cast<CloudColorMode>(colorMode);
    value.clouds.coverage = reader.GetF32(); value.clouds.density = reader.GetF32();
    value.clouds.baseAltitudeKm = reader.GetF32();
    value.clouds.thicknessKm = reader.GetF32(); value.clouds.shapeScaleKm = reader.GetF32();
    value.clouds.erosion = reader.GetF32(); value.clouds.detail = reader.GetF32();
    value.clouds.windDirectionDegrees = reader.GetF32();
    value.clouds.windSpeedKmPerSecond = reader.GetF32();
    value.clouds.litColor = reader.GetU32(); value.clouds.shadowColor = reader.GetU32();
    value.clouds.fireColor = reader.GetU32(); value.clouds.fireEmission = reader.GetF32();
    value.clouds.silverLining = reader.GetF32(); GetBool(reader, value.heightFog.enabled);
    value.heightFog.density = reader.GetF32(); value.heightFog.baseHeight = reader.GetF32();
    value.heightFog.heightFalloff = reader.GetF32();
    value.heightFog.maxDistance = reader.GetF32();
    value.heightFog.startDistance = reader.GetF32();
    value.heightFog.inscatteringColor = reader.GetU32();
    value.heightFog.inscatteringIntensity = reader.GetF32();
    value.heightFog.directionalAnisotropy = reader.GetF32();
    return reader.Ok() && colorMode <= static_cast<std::uint8_t>(CloudColorMode::Fire)
        && ValidateEnvironment(value);
}

} // namespace Concord::Asset::Detail::CookedSceneCodec
