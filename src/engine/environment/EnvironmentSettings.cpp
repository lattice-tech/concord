#include "engine/environment/EnvironmentSettings.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Concord {

namespace {

float Finite(float value, float fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

float NonNegative(float value, float fallback) noexcept
{
    return std::max(Finite(value, fallback), 0.0f);
}

float Unit(float value, float fallback) noexcept
{
    return std::clamp(Finite(value, fallback), 0.0f, 1.0f);
}

float Anisotropy(float value, float fallback) noexcept
{
    return std::clamp(Finite(value, fallback), -0.99f, 0.99f);
}

float WrapDegrees(float value) noexcept
{
    value = Finite(value, 0.0f);
    value = std::fmod(value, 360.0f);
    return value < 0.0f ? value + 360.0f : value;
}

float WrapUnit(float value) noexcept
{
    value = Finite(value, 0.0f);
    value -= std::floor(value);
    return value;
}

} // namespace

EnvironmentSettings SanitizeEnvironmentSettings(EnvironmentSettings settings) noexcept
{
    const EnvironmentSettings defaults;
    settings.sky = SanitizeSkyEnvironment(settings.sky);

    settings.time.timeSeconds = std::isfinite(settings.time.timeSeconds)
        ? std::max(settings.time.timeSeconds, 0.0) : defaults.time.timeSeconds;
    settings.time.timeScale = NonNegative(settings.time.timeScale, defaults.time.timeScale);
    settings.time.fixedStepSeconds = NonNegative(
        settings.time.fixedStepSeconds, defaults.time.fixedStepSeconds);
    settings.time.stepRemainderSeconds = std::isfinite(settings.time.stepRemainderSeconds)
        ? std::max(settings.time.stepRemainderSeconds, 0.0)
        : defaults.time.stepRemainderSeconds;
    if (settings.time.fixedStepSeconds > 0.0f) {
        settings.time.stepRemainderSeconds = std::fmod(
            settings.time.stepRemainderSeconds,
            static_cast<double>(settings.time.fixedStepSeconds));
    } else {
        settings.time.stepRemainderSeconds = 0.0;
    }

    if (settings.clouds.colorMode != CloudColorMode::Physical
        && settings.clouds.colorMode != CloudColorMode::Artistic
        && settings.clouds.colorMode != CloudColorMode::Fire) {
        settings.clouds.colorMode = defaults.clouds.colorMode;
    }
    settings.clouds.coverage = Unit(settings.clouds.coverage, defaults.clouds.coverage);
    settings.clouds.density = NonNegative(settings.clouds.density, defaults.clouds.density);
    settings.clouds.baseAltitudeKm = NonNegative(
        settings.clouds.baseAltitudeKm, defaults.clouds.baseAltitudeKm);
    settings.clouds.thicknessKm = std::max(
        NonNegative(settings.clouds.thicknessKm, defaults.clouds.thicknessKm), 0.001f);
    settings.clouds.shapeScaleKm = std::max(
        NonNegative(settings.clouds.shapeScaleKm, defaults.clouds.shapeScaleKm), 0.001f);
    settings.clouds.erosion = Unit(settings.clouds.erosion, defaults.clouds.erosion);
    settings.clouds.detail = Unit(settings.clouds.detail, defaults.clouds.detail);
    settings.clouds.anvil = Unit(settings.clouds.anvil, defaults.clouds.anvil);
    settings.clouds.precipitation = Unit(
        settings.clouds.precipitation, defaults.clouds.precipitation);
    settings.clouds.windDirectionDegrees = WrapDegrees(settings.clouds.windDirectionDegrees);
    settings.clouds.windSpeedKmPerSecond = NonNegative(
        settings.clouds.windSpeedKmPerSecond, defaults.clouds.windSpeedKmPerSecond);
    settings.clouds.verticalWindKmPerSecond = Finite(
        settings.clouds.verticalWindKmPerSecond, defaults.clouds.verticalWindKmPerSecond);
    settings.clouds.weatherPhase = WrapUnit(settings.clouds.weatherPhase);
    settings.clouds.weatherCyclesPerHour = NonNegative(
        settings.clouds.weatherCyclesPerHour, defaults.clouds.weatherCyclesPerHour);
    settings.clouds.fireEmission = NonNegative(
        settings.clouds.fireEmission, defaults.clouds.fireEmission);
    settings.clouds.phaseAnisotropy = Anisotropy(
        settings.clouds.phaseAnisotropy, defaults.clouds.phaseAnisotropy);
    settings.clouds.silverLining = std::clamp(
        Finite(settings.clouds.silverLining, defaults.clouds.silverLining), 0.0f, 4.0f);
    settings.clouds.primarySteps = std::clamp<std::uint16_t>(
        settings.clouds.primarySteps, 8, 256);
    settings.clouds.lightSteps = std::clamp<std::uint8_t>(settings.clouds.lightSteps, 1, 32);

    settings.heightFog.density = NonNegative(
        settings.heightFog.density, defaults.heightFog.density);
    settings.heightFog.baseHeight = Finite(
        settings.heightFog.baseHeight, defaults.heightFog.baseHeight);
    settings.heightFog.heightFalloff = NonNegative(
        settings.heightFog.heightFalloff, defaults.heightFog.heightFalloff);
    settings.heightFog.maxDistance = NonNegative(
        settings.heightFog.maxDistance, defaults.heightFog.maxDistance);
    settings.heightFog.startDistance = std::clamp(
        NonNegative(settings.heightFog.startDistance, defaults.heightFog.startDistance),
        0.0f, settings.heightFog.maxDistance);
    settings.heightFog.inscatteringIntensity = NonNegative(
        settings.heightFog.inscatteringIntensity, defaults.heightFog.inscatteringIntensity);
    settings.heightFog.directionalAnisotropy = Anisotropy(
        settings.heightFog.directionalAnisotropy, defaults.heightFog.directionalAnisotropy);

    return settings;
}

EnvironmentTimeSettings AdvanceEnvironmentTime(
    EnvironmentTimeSettings time, double realDeltaSeconds) noexcept
{
    EnvironmentSettings wrapper;
    wrapper.time = time;
    time = SanitizeEnvironmentSettings(wrapper).time;
    if (time.paused || !std::isfinite(realDeltaSeconds) || realDeltaSeconds <= 0.0) {
        return time;
    }

    double increment = realDeltaSeconds * static_cast<double>(time.timeScale);
    if (!std::isfinite(increment)) {
        time.timeSeconds = std::numeric_limits<double>::max();
        time.stepRemainderSeconds = 0.0;
        return time;
    }
    if (time.fixedStepSeconds > 0.0f) {
        const double step = static_cast<double>(time.fixedStepSeconds);
        increment += time.stepRemainderSeconds;
        const double stepCount = std::floor(increment / step);
        increment = stepCount * step;
        time.stepRemainderSeconds = std::fmod(
            realDeltaSeconds * static_cast<double>(time.timeScale)
                + time.stepRemainderSeconds,
            step);
    }
    time.timeSeconds = std::min(
        time.timeSeconds + increment, std::numeric_limits<double>::max());
    return time;
}

} // namespace Concord
