#ifndef CONCORD_SKYENVIRONMENT_H
#define CONCORD_SKYENVIRONMENT_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Concord {

/** Selects how background pixels are generated for a scene. */
enum class SkyMode : std::uint8_t {
    /** Draw one authored color behind the scene. */
    Solid = 0,

    /** Draw a camera-oriented zenith/horizon/ground gradient with a sun disk. */
    Procedural,
};

/**
 * Legacy neutral sky chromaticity used by fallback helpers that do not receive
 * a scene-authored SkyEnvironment. Scene rendering uses the environment's
 * independent visible-sky and ambient colors instead.
 *
 * `kSkyColor` is the sky's colour at unit intensity (sRGB); the ambient fill and
 * the background are the same colour scaled to two different intensities — the
 * sky is brighter as seen directly than as bounced indirect light. Values are
 * intentionally close to neutral with a faint cool bias (atmospheric scatter),
 * not a saturated blue, so shadows lit by it read as natural rather than tinted.
 */
inline constexpr float kSkyColor[3] = {0.58f, 0.66f, 0.82f};

/** Fraction of the sky colour that becomes indirect ambient fill (sRGB). */
inline constexpr float kSkyAmbientLevel = 0.48f;

/** Fraction of the sky colour shown as the window background (sRGB). */
inline constexpr float kSkyBackgroundLevel = 0.38f;

/**
 * Warm ground-bounce chromaticity mixed into ambient on down-facing surfaces.
 * Keeps sun shadows from reading as a single flat grey: open sky stays cool,
 * contact with the ground picks up a gentle warm fill.
 */
inline constexpr float kGroundBounceColor[3] = {0.72f, 0.58f, 0.42f};

/** Strength of the ground-bounce term relative to sky ambient (sRGB). */
inline constexpr float kGroundBounceLevel = 0.28f;

/**
 * Scene-level sky and indirect-light settings.
 *
 * The first implementation is intentionally texture-free: Procedural mode
 * reconstructs a world-space view ray in the final HDR present pass, blends
 * the authored zenith, horizon and ground colors, then places a disk at the
 * active directional sun. This establishes the skybox contract without
 * committing the public API to a particular cubemap asset format.
 */
struct SkyEnvironment {
    /** Background generation mode. */
    SkyMode mode = SkyMode::Procedural;

    /** Solid-mode background, packed 0xRRGGBBAA in sRGB. */
    std::uint32_t solidColor = 0x394250ffu;

    /** Procedural color looking straight up, packed 0xRRGGBBAA in sRGB. */
    std::uint32_t zenithColor = 0x537bb8ffu;

    /** Procedural color at the horizon, packed 0xRRGGBBAA in sRGB. */
    std::uint32_t horizonColor = 0xa8bfd8ffu;

    /** Procedural color below the horizon, packed 0xRRGGBBAA in sRGB. */
    std::uint32_t groundColor = 0x31343bffu;

    /** Indirect sky-light chromaticity, packed 0xRRGGBBAA in sRGB. */
    std::uint32_t ambientColor = 0x94a8d1ffu;

    /** Multiplier on the visible sky. */
    float intensity = 0.72f;

    /** Fraction of ambientColor used as diffuse/specular environment fill. */
    float ambientIntensity = kSkyAmbientLevel;

    /** Ambient multiplier at procedural-sky night, before dawn interpolation. */
    float nightAmbientIntensity = 0.025f;

    /** Shape exponent for the procedural horizon transition. */
    float horizonFalloff = 0.65f;

    /** Multiplier on the visible directional-light sun disk and halo. */
    float sunDiskIntensity = 1.0f;

    /** Whether Procedural mode draws the directional-light sun disk. */
    bool sunDisk = true;

    /** Enables the built-in ray-marched dynamic cloud layer. */
    bool clouds = false;

    /** Cloud coverage and optical-density controls in [0, 1] and [0, +inf). */
    float cloudCoverage = 0.45f;
    float cloudDensity = 0.8f;

    /** World-space cloud base and thickness used by the sky ray march. */
    float cloudBaseHeight = 1500.0f;
    float cloudThickness = 4000.0f;

    /** Horizontal noise offsets, advanced deterministically by EnvironmentSettings. */
    float cloudOffsetEast = 0.0f;
    float cloudOffsetNorth = 0.0f;

    /** Cloud formation, edge erosion, and forward-scattering controls. */
    float cloudScale = 12000.0f;
    float cloudErosion = 0.55f;
    float cloudDetail = 0.5f;
    float cloudSilverLining = 1.0f;

    /** Lit, shadow, and emissive cloud colors packed as 0xRRGGBBAA in sRGB. */
    std::uint32_t cloudLitColor = 0xfff4e6ffu;
    std::uint32_t cloudShadowColor = 0x687080ffu;
    std::uint32_t cloudFireColor = 0xff6a18ffu;

    /** Fire-cloud emission; zero selects ordinary physically lit clouds. */
    float cloudFireEmission = 0.0f;

    /** Enables distance/height-aware atmospheric fog in the sky ray march. */
    bool volumetricFog = false;
    float fogDensity = 0.02f;
    float fogBaseHeight = 0.0f;
    float fogHeightFalloff = 0.2f;
    std::uint32_t fogColor = 0xb7c8d8ffu;
};

/** The immutable default used when a Scene does not author an environment. */
inline constexpr SkyEnvironment kDefaultSkyEnvironment{};

/** Returns finite, non-negative environment controls suitable for rendering. */
inline SkyEnvironment SanitizeSkyEnvironment(SkyEnvironment environment) noexcept
{
    if (environment.mode != SkyMode::Solid && environment.mode != SkyMode::Procedural) {
        environment.mode = kDefaultSkyEnvironment.mode;
    }
    const auto sanitize = [](float value, float fallback) noexcept {
        return std::isfinite(value) ? std::max(value, 0.0f) : fallback;
    };
    environment.intensity = sanitize(environment.intensity, kDefaultSkyEnvironment.intensity);
    environment.ambientIntensity = sanitize(
        environment.ambientIntensity, kDefaultSkyEnvironment.ambientIntensity);
    environment.nightAmbientIntensity = sanitize(
        environment.nightAmbientIntensity, kDefaultSkyEnvironment.nightAmbientIntensity);
    environment.horizonFalloff = sanitize(
        environment.horizonFalloff, kDefaultSkyEnvironment.horizonFalloff);
    environment.sunDiskIntensity = sanitize(
        environment.sunDiskIntensity, kDefaultSkyEnvironment.sunDiskIntensity);
    environment.cloudCoverage = std::clamp(
        sanitize(environment.cloudCoverage, kDefaultSkyEnvironment.cloudCoverage), 0.0f, 1.0f);
    environment.cloudDensity = sanitize(environment.cloudDensity, kDefaultSkyEnvironment.cloudDensity);
    environment.cloudBaseHeight = sanitize(
        environment.cloudBaseHeight, kDefaultSkyEnvironment.cloudBaseHeight);
    environment.cloudThickness = std::max(
        sanitize(environment.cloudThickness, kDefaultSkyEnvironment.cloudThickness), 1.0f);
    environment.cloudOffsetEast = std::isfinite(environment.cloudOffsetEast)
        ? environment.cloudOffsetEast : 0.0f;
    environment.cloudOffsetNorth = std::isfinite(environment.cloudOffsetNorth)
        ? environment.cloudOffsetNorth : 0.0f;
    environment.cloudScale = std::max(
        sanitize(environment.cloudScale, kDefaultSkyEnvironment.cloudScale), 1.0f);
    environment.cloudErosion = std::clamp(
        sanitize(environment.cloudErosion, kDefaultSkyEnvironment.cloudErosion), 0.0f, 1.0f);
    environment.cloudDetail = std::clamp(
        sanitize(environment.cloudDetail, kDefaultSkyEnvironment.cloudDetail), 0.0f, 1.0f);
    environment.cloudSilverLining = sanitize(
        environment.cloudSilverLining, kDefaultSkyEnvironment.cloudSilverLining);
    environment.cloudFireEmission = sanitize(
        environment.cloudFireEmission, kDefaultSkyEnvironment.cloudFireEmission);
    environment.fogDensity = sanitize(environment.fogDensity, kDefaultSkyEnvironment.fogDensity);
    environment.fogBaseHeight = std::isfinite(environment.fogBaseHeight)
        ? environment.fogBaseHeight : kDefaultSkyEnvironment.fogBaseHeight;
    environment.fogHeightFalloff = sanitize(
        environment.fogHeightFalloff, kDefaultSkyEnvironment.fogHeightFalloff);
    return environment;
}

/** Writes a packed sRGB color as normalized RGB floats. */
inline void UnpackSkyColor(std::uint32_t color, float out[3]) noexcept
{
    out[0] = static_cast<float>((color >> 24) & 0xffu) / 255.0f;
    out[1] = static_cast<float>((color >> 16) & 0xffu) / 255.0f;
    out[2] = static_cast<float>((color >> 8) & 0xffu) / 255.0f;
}

/** Writes the ambient fill colour (sRGB, the shader linearizes it) into `out`. */
inline void SkyAmbientColor(float out[3]) noexcept
{
    for (std::size_t channel = 0; channel < 3; ++channel) {
        const float linear = std::pow(kSkyColor[channel], 2.2f) * kSkyAmbientLevel;
        out[channel] = std::pow(linear, 1.0f / 2.2f);
    }
}

/** Writes one environment's ambient color encoded for shader linearization. */
inline void SkyAmbientColor(const SkyEnvironment& environment, float intensity,
                            float out[3]) noexcept
{
    UnpackSkyColor(environment.ambientColor, out);
    const float safeIntensity = std::isfinite(intensity) ? std::max(intensity, 0.0f) : 0.0f;
    for (std::size_t channel = 0; channel < 3; ++channel) {
        const float linear = std::pow(std::max(out[channel], 0.0f), 2.2f)
            * safeIntensity;
        out[channel] = std::pow(linear, 1.0f / 2.2f);
    }
}

/** Writes one environment's authored daytime ambient fill color. */
inline void SkyAmbientColor(const SkyEnvironment& environment, float out[3]) noexcept
{
    SkyAmbientColor(environment, environment.ambientIntensity, out);
}

/** Writes the warm ground-bounce colour (sRGB) into `out`. */
inline void GroundBounceColor(float out[3]) noexcept
{
    out[0] = kGroundBounceColor[0] * kGroundBounceLevel;
    out[1] = kGroundBounceColor[1] * kGroundBounceLevel;
    out[2] = kGroundBounceColor[2] * kGroundBounceLevel;
}

/**
 * The window background as a packed `0xRRGGBBAA` clear colour, computed from the
 * same sky chromaticity as the ambient fill so background and lighting match.
 */
inline std::uint32_t SkyBackgroundRgba() noexcept
{
    const auto channel = [](float linearish) -> std::uint32_t {
        const float clamped = std::clamp(linearish, 0.0f, 1.0f);
        return static_cast<std::uint32_t>(std::lround(clamped * 255.0f));
    };
    const std::uint32_t r = channel(kSkyColor[0] * kSkyBackgroundLevel);
    const std::uint32_t g = channel(kSkyColor[1] * kSkyBackgroundLevel);
    const std::uint32_t b = channel(kSkyColor[2] * kSkyBackgroundLevel);
    return (r << 24) | (g << 16) | (b << 8) | 0xffu;
}

/** Returns the conservative solid clear used before the procedural sky pass. */
inline std::uint32_t SkyBackgroundRgba(const SkyEnvironment& environment) noexcept
{
    if (environment.mode == SkyMode::Solid) {
        return environment.solidColor | 0xffu;
    }

    float horizon[3];
    UnpackSkyColor(environment.horizonColor, horizon);
    const float authoredIntensity = std::isfinite(environment.intensity)
        ? environment.intensity : kDefaultSkyEnvironment.intensity;
    const float intensity = std::max(authoredIntensity, 0.0f) * 0.55f;
    const auto channel = [intensity](float value) -> std::uint32_t {
        return static_cast<std::uint32_t>(std::lround(
            std::clamp(value * intensity, 0.0f, 1.0f) * 255.0f));
    };
    return (channel(horizon[0]) << 24)
        | (channel(horizon[1]) << 16)
        | (channel(horizon[2]) << 8)
        | 0xffu;
}

/** Linearized sky clear used by the HDR cubemap before the mesh tone-map pass. */
inline std::uint32_t SkyReflectionClearRgba() noexcept
{
    const auto channel = [](float srgb) -> std::uint32_t {
        const float linear = std::pow(std::clamp(srgb, 0.0f, 1.0f), 2.2f);
        return static_cast<std::uint32_t>(std::lround(linear * 255.0f));
    };
    const std::uint32_t r = channel(kSkyColor[0] * kSkyBackgroundLevel);
    const std::uint32_t g = channel(kSkyColor[1] * kSkyBackgroundLevel);
    const std::uint32_t b = channel(kSkyColor[2] * kSkyBackgroundLevel);
    return (r << 24) | (g << 16) | (b << 8) | 0xffu;
}

/** Linearized clear color used by real-time reflection captures. */
inline std::uint32_t SkyReflectionClearRgba(const SkyEnvironment& environment) noexcept
{
    const std::uint32_t source = environment.mode == SkyMode::Solid
        ? environment.solidColor : environment.horizonColor;
    float color[3];
    UnpackSkyColor(source, color);
    const float authoredIntensity = std::isfinite(environment.intensity)
        ? environment.intensity : kDefaultSkyEnvironment.intensity;
    const float intensity = std::max(authoredIntensity, 0.0f) * 0.55f;
    const auto channel = [intensity](float value) -> std::uint32_t {
        const float linear = std::pow(std::clamp(value * intensity, 0.0f, 1.0f), 2.2f);
        return static_cast<std::uint32_t>(std::lround(linear * 255.0f));
    };
    return (channel(color[0]) << 24)
        | (channel(color[1]) << 16)
        | (channel(color[2]) << 8)
        | 0xffu;
}

} // namespace Concord

#endif // CONCORD_SKYENVIRONMENT_H
