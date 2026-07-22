#ifndef CONCORD_ENVIRONMENTSETTINGS_H
#define CONCORD_ENVIRONMENTSETTINGS_H

#include "Concord/CExport.h"
#include "engine/render/frame/SkyEnvironment.h"

#include <cstdint>

namespace Concord {

/** Selects how authored cloud colors are produced. */
enum class CloudColorMode : std::uint8_t {
    /** Derive scattering colors from the atmosphere and active sun. */
    Physical = 0,
    /** Use explicit lit, shadow, and ambient colors. */
    Artistic,
    /** Use explicit emissive fire and smoke colors. */
    Fire,
};

/** Deterministic simulation clock shared by wind and weather animation. */
struct EnvironmentTimeSettings {
    /** Absolute authored simulation time in seconds. */
    double timeSeconds = 0.0;
    /** Simulation seconds advanced per real second. */
    float timeScale = 1.0f;
    /** Optional fixed step; zero leaves time continuous. */
    float fixedStepSeconds = 1.0f / 60.0f;
    /** Unconsumed scaled time retained between fixed-step advances. */
    double stepRemainderSeconds = 0.0;
    /** Prevents advancement while preserving the current time. */
    bool paused = false;
};

/** Dynamic, deterministic volumetric cloud authoring controls. */
struct VolumetricCloudSettings {
    /** Enables the cloud layer. */
    bool enabled = true;
    /** Cloud color and lighting model. */
    CloudColorMode colorMode = CloudColorMode::Physical;
    /** Stable noise and weather-map seed. */
    std::uint32_t seed = 1;
    /** Fraction of the weather map covered by clouds in [0, 1]. */
    float coverage = 0.45f;
    /** Optical density multiplier. */
    float density = 0.8f;
    /** Cloud layer base altitude in kilometers. */
    float baseAltitudeKm = 1.5f;
    /** Cloud layer thickness in kilometers. */
    float thicknessKm = 4.0f;
    /** Low-frequency formation scale in kilometers. */
    float shapeScaleKm = 12.0f;
    /** High-frequency edge erosion in [0, 1]. */
    float erosion = 0.55f;
    /** Fine detail strength in [0, 1]. */
    float detail = 0.5f;
    /** Anvil shaping amount in [0, 1]. */
    float anvil = 0.0f;
    /** Precipitation/storm loading in [0, 1]. */
    float precipitation = 0.0f;
    /** Wind direction clockwise from geographic north. */
    float windDirectionDegrees = 0.0f;
    /** Horizontal wind speed in kilometers per second. */
    float windSpeedKmPerSecond = 0.01f;
    /** Vertical evolution speed in kilometers per second. */
    float verticalWindKmPerSecond = 0.0f;
    /** Deterministic weather-map phase in [0, 1). */
    float weatherPhase = 0.0f;
    /** Weather evolution cycles per simulation hour. */
    float weatherCyclesPerHour = 0.05f;
    /** Physical/artistic lit cloud color, packed 0xRRGGBBAA in sRGB. */
    std::uint32_t litColor = 0xfff4e6ffu;
    /** Artistic cloud shadow color, packed 0xRRGGBBAA in sRGB. */
    std::uint32_t shadowColor = 0x687080ffu;
    /** Artistic ambient cloud color, packed 0xRRGGBBAA in sRGB. */
    std::uint32_t ambientColor = 0x91a7c7ffu;
    /** Fire-mode emissive core color, packed 0xRRGGBBAA in sRGB. */
    std::uint32_t fireColor = 0xff6a18ffu;
    /** Fire-mode smoke color, packed 0xRRGGBBAA in sRGB. */
    std::uint32_t fireSmokeColor = 0x211a1affu;
    /** Fire-mode emissive luminance multiplier. */
    float fireEmission = 4.0f;
    /** Forward-scattering phase anisotropy in [-0.99, 0.99]. */
    float phaseAnisotropy = 0.65f;
    /** Silver-lining strength in [0, 4]. */
    float silverLining = 1.0f;
    /** Number of intended primary ray-march samples in [8, 256]. */
    std::uint16_t primarySteps = 64;
    /** Number of intended light-cone samples in [1, 32]. */
    std::uint8_t lightSteps = 6;
};

/** Scene-wide exponential height fog, excluding local volumetric smoke. */
struct HeightFogSettings {
    /** Enables global height fog. */
    bool enabled = false;
    /** Extinction density at baseHeight. */
    float density = 0.02f;
    /** World-space height at which density is authored. */
    float baseHeight = 0.0f;
    /** Exponential density falloff per world unit. */
    float heightFalloff = 0.2f;
    /** Distance at which fog evaluation may stop. */
    float maxDistance = 1000.0f;
    /** Start distance left completely clear. */
    float startDistance = 0.0f;
    /** Fog in-scattering color packed as 0xRRGGBBAA in sRGB. */
    std::uint32_t inscatteringColor = 0xb7c8d8ffu;
    /** In-scattering luminance multiplier. */
    float inscatteringIntensity = 1.0f;
    /** Directional phase anisotropy in [-0.99, 0.99]. */
    float directionalAnisotropy = 0.2f;
};

/** Complete caller-facing environment authoring description. */
struct EnvironmentSettings {
    /** Existing visible sky and ambient-light controls. */
    SkyEnvironment sky{};
    /** Shared deterministic animation clock. */
    EnvironmentTimeSettings time{};
    /** Dynamic volumetric cloud controls. */
    VolumetricCloudSettings clouds{};
    /** Global exponential height fog controls. */
    HeightFogSettings heightFog{};
};

/** Returns a finite, range-safe environment description. */
CENGINE_API EnvironmentSettings SanitizeEnvironmentSettings(
    EnvironmentSettings settings) noexcept;

/**
 * Advances the authored deterministic clock.
 * @param time Current clock settings.
 * @param realDeltaSeconds Non-negative real elapsed time.
 * @return Sanitized clock advanced and optionally quantized to its fixed step.
 */
CENGINE_API EnvironmentTimeSettings AdvanceEnvironmentTime(
    EnvironmentTimeSettings time, double realDeltaSeconds) noexcept;

} // namespace Concord

#endif // CONCORD_ENVIRONMENTSETTINGS_H
