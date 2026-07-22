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
            fog.enabled = false;
            break;
        case WeatherCondition::PartlyCloudy:
            clouds.coverage = 0.45f;
            clouds.density = 0.8f;
            fog.enabled = false;
            break;
        case WeatherCondition::Overcast:
            clouds.coverage = 0.8f;
            clouds.density = 1.1f;
            fog.enabled = true;
            fog.density = 0.02f;
            break;
        case WeatherCondition::Storm:
            clouds.coverage = 0.95f;
            clouds.density = 1.5f;
            fog.enabled = true;
            fog.density = 0.05f;
            break;
    }
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
