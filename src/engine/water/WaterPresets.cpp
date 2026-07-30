#include "engine/water/WaterPresets.h"

#include <algorithm>
#include <cmath>

namespace Concord::Water::Presets {
namespace {

/** Grid resolution at roughly one quad per world unit, bounded for sanity. */
std::uint32_t TessellationFor(float width, float length) noexcept
{
    const float longest = std::max(std::fabs(width), std::fabs(length));
    if (!std::isfinite(longest)) {
        return 64u;
    }
    return static_cast<std::uint32_t>(std::clamp(longest, 16.0f, 256.0f));
}

WaterSurfaceDesc Base(float width, float length) noexcept
{
    WaterSurfaceDesc desc;
    desc.width = width;
    desc.length = length;
    desc.tessellation = TessellationFor(width, length);
    return desc;
}

} // namespace

WaterSurfaceDesc StillLake(float width, float length) noexcept
{
    WaterSurfaceDesc desc = Base(width, length);
    desc.kind = WaterKind::Lake;
    desc.motion = WaterMotion::Still;
    // A flat surface needs no vertex detail, only a clean reflection.
    desc.tessellation = 8u;
    desc.waveCount = 0u;
    desc.waveAmplitude = 0.0f;
    desc.waveSpeed = 0.0f;
    desc.depth = 6.0f;
    desc.absorption = 0.42f;
    // Near-mirror: the glint stays tight and the reflection stays readable.
    desc.roughness = 0.02f;
    desc.refractionStrength = 0.12f;
    desc.foamIntensity = 0.25f;
    desc.foamWidth = 0.35f;
    desc.planarReflection = true;
    // No wind: a still lake has no spectrum to bake.
    desc.wind.windSpeed = 0.0f;
    desc.optics.subsurfaceScattering = 0.25f;
    return desc;
}

WaterSurfaceDesc WavyLake(float width, float length) noexcept
{
    WaterSurfaceDesc desc = Base(width, length);
    desc.kind = WaterKind::Lake;
    desc.motion = WaterMotion::Dynamic;
    desc.depth = 8.0f;
    desc.absorption = 0.45f;
    desc.roughness = 0.075f;
    desc.refractionStrength = 0.4f;
    desc.waveCount = 4u;
    desc.waveAmplitude = 0.18f;
    desc.waveLength = 9.0f;
    desc.waveSteepness = 0.6f;
    desc.waveSpeed = 1.3f;
    desc.waveDirectionDegrees = 25.0f;
    desc.foamWidth = 0.7f;
    desc.foamIntensity = 0.8f;
    // A gentle breeze carries enough chop to break up the reflection without
    // tearing it. Short fetch keeps the swell local: lake-scale, not oceanic.
    desc.wind.windSpeed = 5.5f;
    desc.wind.windDirectionDegrees = 25.0f;
    desc.wind.fetchMeters = 600.0f;
    desc.wind.choppiness = 0.85f;
    desc.wind.spreadDegrees = 60.0f;
    desc.wind.amplitudeScale = 0.6f;
    desc.optics.subsurfaceScattering = 0.4f;
    desc.optics.sunGlintIntensity = 1.0f;
    desc.planarReflection = true;
    return desc;
}

WaterSurfaceDesc StaticRiver(float width, float length) noexcept
{
    WaterSurfaceDesc desc = Base(width, length);
    desc.kind = WaterKind::River;
    desc.motion = WaterMotion::Still;
    desc.tessellation = 24u;
    desc.waveCount = 0u;
    desc.waveAmplitude = 0.0f;
    desc.waveSpeed = 0.0f;
    desc.flowSpeed = 0.0f;
    // Shallow and clear: a channel is read through, not into.
    desc.depth = 1.2f;
    desc.absorption = 0.22f;
    desc.shallowColor = 0x6FBFC4ffu;
    desc.deepColor = 0x1D4B52ffu;
    desc.roughness = 0.03f;
    desc.refractionStrength = 0.3f;
    desc.foamWidth = 0.4f;
    desc.foamIntensity = 0.5f;
    desc.wind.windSpeed = 0.0f;
    desc.optics.subsurfaceScattering = 0.3f;
    desc.planarReflection = true;
    return desc;
}

WaterSurfaceDesc FlowingRiver(float width, float length) noexcept
{
    WaterSurfaceDesc desc = StaticRiver(width, length);
    desc.motion = WaterMotion::Dynamic;
    desc.tessellation = TessellationFor(width, length);
    // Short, steep chop travelling with the current, not a lake swell.
    desc.waveCount = 3u;
    desc.waveAmplitude = 0.055f;
    desc.waveLength = 2.2f;
    desc.waveSteepness = 0.75f;
    desc.waveSpeed = 2.4f;
    desc.waveDirectionDegrees = 90.0f;
    // The channel runs along local Z, so the current follows it.
    desc.flowSpeed = 2.0f;
    desc.flowDirectionDegrees = 90.0f;
    desc.roughness = 0.09f;
    desc.refractionStrength = 0.45f;
    desc.foamWidth = 0.55f;
    desc.foamIntensity = 0.95f;
    // A light breeze aligned with the channel, narrow spread so the chop
    // runs in lanes downstream rather than fanning out.
    desc.wind.windSpeed = 3.0f;
    desc.wind.windDirectionDegrees = 90.0f;
    desc.wind.fetchMeters = 120.0f;
    desc.wind.choppiness = 1.1f;
    desc.wind.spreadDegrees = 18.0f;
    desc.wind.amplitudeScale = 0.45f;
    desc.optics.subsurfaceScattering = 0.35f;
    desc.optics.foamGrainScale = 1.3f;
    // Flowing water never holds a clean mirror image.
    desc.planarReflection = false;
    return desc;
}

WaterSurfaceDesc OpenOcean(float width, float length) noexcept
{
    WaterSurfaceDesc desc = Base(width, length);
    desc.kind = WaterKind::Ocean;
    desc.motion = WaterMotion::Dynamic;
    desc.depth = 30.0f;
    desc.absorption = 0.55f;
    desc.shallowColor = 0x1C6E8Effu;
    desc.deepColor = 0x02181Fu;
    desc.roughness = 0.11f;
    desc.refractionStrength = 0.08f;
    // The ocean surface is large and its depth matters less than its waves;
    // the clipmap mesh covers whatever extent is authored.
    desc.tessellation = TessellationFor(width, length);
    desc.waveCount = 4u;
    desc.waveAmplitude = 0.0f;
    desc.waveLength = 0.0f;
    desc.waveSpeed = 0.0f;
    // A moderate open-ocean breeze: Hs ≈ 0.21·8²/9.81 ≈ 1.4m, fetch effectively
    // unlimited so long swells develop. Broad spread gives the directionless,
    // rolling look of open water.
    desc.wind.windSpeed = 8.0f;
    desc.wind.windDirectionDegrees = 45.0f;
    desc.wind.fetchMeters = 10000.0f;
    desc.wind.choppiness = 1.15f;
    desc.wind.spreadDegrees = 65.0f;
    desc.wind.amplitudeScale = 1.0f;
    desc.foamWidth = 1.2f;
    desc.foamIntensity = 1.0f;
    desc.optics.subsurfaceScattering = 0.65f;
    desc.optics.scatteringColor = 0x0E4A60ffu;
    desc.optics.sunGlintIntensity = 1.2f;
    desc.optics.foamGrainScale = 1.1f;
    // Open ocean still reflects the sky strongly; planar reflection of the
    // real scene is too expensive across a whole ocean so the analytic sky +
    // env probe carries it.
    desc.planarReflection = false;
    return desc;
}

} // namespace Concord::Water::Presets