#include "engine/environment/Weather.h"

#include <algorithm>
#include <cmath>

namespace Concord {

namespace {

float Clamp01(float value) noexcept
{
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

} // namespace

void Weather::Apply(EnvironmentSettings& settings, WeatherCondition condition) noexcept
{
    VolumetricCloudSettings& clouds = settings.clouds;
    HeightFogSettings& fog = settings.heightFog;
    clouds.enabled = true;
    switch (condition) {
        case WeatherCondition::Clear:
            clouds.coverage = 0.15f;
            clouds.density = 0.5f;
            clouds.precipitation = 0.0f;
            fog.enabled = false;
            break;
        case WeatherCondition::PartlyCloudy:
            clouds.coverage = 0.45f;
            clouds.density = 0.8f;
            clouds.precipitation = 0.0f;
            fog.enabled = false;
            break;
        case WeatherCondition::Overcast:
            clouds.coverage = 0.8f;
            clouds.density = 1.1f;
            clouds.precipitation = 0.1f;
            fog.enabled = true;
            fog.density = 0.02f;
            break;
        case WeatherCondition::Storm:
            clouds.coverage = 0.95f;
            clouds.density = 1.5f;
            clouds.precipitation = 0.8f;
            fog.enabled = true;
            fog.density = 0.05f;
            break;
    }
}

void Weather::SetRainIntensity(EnvironmentSettings& settings, float intensity) noexcept
{
    // Interface only: the precipitation amount is stored so a future renderer
    // can consume it. No rain/snow is drawn by the engine yet.
    const float amount = Clamp01(intensity);
    settings.clouds.precipitation = amount;
    // Heavier rain reads as thicker, darker, more complete cloud cover.
    settings.clouds.coverage = std::max(settings.clouds.coverage, 0.5f + 0.45f * amount);
    settings.clouds.density = std::max(settings.clouds.density, 0.8f + 0.7f * amount);
}

void Weather::SetCoverage(EnvironmentSettings& settings, float coverage) noexcept
{
    settings.clouds.coverage = Clamp01(coverage);
}

void Weather::SetWind(EnvironmentSettings& settings, float directionDegrees,
                      float speedKmPerSecond) noexcept
{
    if (std::isfinite(directionDegrees)) {
        settings.clouds.windDirectionDegrees = directionDegrees;
    }
    if (std::isfinite(speedKmPerSecond) && speedKmPerSecond >= 0.0f) {
        settings.clouds.windSpeedKmPerSecond = speedKmPerSecond;
    }
}

} // namespace Concord
