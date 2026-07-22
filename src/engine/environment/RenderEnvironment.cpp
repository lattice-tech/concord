#include "engine/environment/RenderEnvironment.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Concord {

namespace {

constexpr double kPi = 3.14159265358979323846;

float FiniteFloat(double value) noexcept
{
    constexpr double limit = static_cast<double>(std::numeric_limits<float>::max());
    return static_cast<float>(std::clamp(value, -limit, limit));
}

RenderCloudAnimation ResolveCloudAnimation(const EnvironmentSettings& settings) noexcept
{
    const double seconds = settings.time.timeSeconds;
    const double radians = static_cast<double>(settings.clouds.windDirectionDegrees)
        * kPi / 180.0;
    const double distance = seconds
        * static_cast<double>(settings.clouds.windSpeedKmPerSecond);
    RenderCloudAnimation animation;
    animation.offsetEastKm = FiniteFloat(std::sin(radians) * distance);
    animation.offsetNorthKm = FiniteFloat(std::cos(radians) * distance);
    return animation;
}

} // namespace

RenderEnvironment::RenderEnvironment() noexcept
    : RenderEnvironment(SanitizeEnvironmentSettings({}))
{
}

RenderEnvironment::RenderEnvironment(EnvironmentSettings settings) noexcept
    : m_settings(SanitizeEnvironmentSettings(settings))
    , m_cloudAnimation(ResolveCloudAnimation(m_settings))
{
}

RenderEnvironment ResolveRenderEnvironment(EnvironmentSettings settings) noexcept
{
    return RenderEnvironment(settings);
}

} // namespace Concord
