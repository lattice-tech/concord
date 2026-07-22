#include "engine/environment/DayNightCycle.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Concord {

namespace {

float WrapHours(float hours) noexcept
{
    if (!std::isfinite(hours)) {
        return 0.0f;
    }
    hours = std::fmod(hours, 24.0f);
    if (hours < 0.0f) {
        hours += 24.0f;
    }
    return hours;
}

float Clamp01(float value) noexcept
{
    return std::clamp(value, 0.0f, 1.0f);
}

/** Hermite smoothstep matching the shaders' edge ramp. */
float SmoothStep(float edge0, float edge1, float x) noexcept
{
    if (edge0 == edge1) {
        return x < edge0 ? 0.0f : 1.0f;
    }
    const float t = Clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

struct Rgb {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

Rgb Lerp(const Rgb& a, const Rgb& b, float t) noexcept
{
    return Rgb{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

std::uint32_t Pack(const Rgb& color) noexcept
{
    const auto channel = [](float value) -> std::uint32_t {
        return static_cast<std::uint32_t>(std::lround(Clamp01(value) * 255.0f));
    };
    return (channel(color.r) << 24) | (channel(color.g) << 16) | (channel(color.b) << 8) | 0xffu;
}

// Palettes in normalized sRGB. Day is a clear blue sky; golden is the warm
// low-sun horizon; night is a dim cool sky.
constexpr Rgb kDayZenith{0.274f, 0.439f, 0.698f};
constexpr Rgb kDayHorizon{0.721f, 0.800f, 0.862f};
constexpr Rgb kDayGround{0.168f, 0.176f, 0.203f};
constexpr Rgb kDayAmbient{0.580f, 0.658f, 0.819f};

constexpr Rgb kGoldenZenith{0.235f, 0.313f, 0.549f};
constexpr Rgb kGoldenHorizon{0.901f, 0.470f, 0.235f};
constexpr Rgb kGoldenGround{0.196f, 0.156f, 0.149f};
constexpr Rgb kGoldenAmbient{0.705f, 0.549f, 0.470f};

constexpr Rgb kNightZenith{0.039f, 0.054f, 0.125f};
constexpr Rgb kNightHorizon{0.098f, 0.117f, 0.215f};
constexpr Rgb kNightGround{0.031f, 0.035f, 0.054f};
constexpr Rgb kNightAmbient{0.070f, 0.094f, 0.196f};

// Warm cloud colors used at golden hour ("fire clouds").
constexpr Rgb kFireLit{1.0f, 0.678f, 0.400f};
constexpr Rgb kFireCore{1.0f, 0.416f, 0.094f};

} // namespace

DayNightCycle::DayNightCycle(DayNightConfig config) noexcept
    : m_config(config)
    , m_civilHour(WrapHours(config.startHour))
{
}

void DayNightCycle::Advance(float realDeltaSeconds) noexcept
{
    if (!std::isfinite(realDeltaSeconds) || realDeltaSeconds <= 0.0f) {
        return;
    }
    const float secondsPerDay = m_config.secondsPerDay;
    if (!std::isfinite(secondsPerDay) || std::fabs(secondsPerDay) < 1e-3f) {
        return;
    }
    const float hoursPerSecond = 24.0f / secondsPerDay;
    m_civilHour = WrapHours(m_civilHour + realDeltaSeconds * hoursPerSecond * m_config.timeScale);
}

void DayNightCycle::SetCivilHour(float hour) noexcept
{
    m_civilHour = WrapHours(hour);
}

void DayNightCycle::ApplySky(EnvironmentSettings& settings, float sunElevationDegrees) const noexcept
{
    const float elevation = std::isfinite(sunElevationDegrees) ? sunElevationDegrees : 0.0f;

    // day: 0 at night, 1 in full daylight. golden: peaks as the sun crosses the
    // horizon (sunrise/sunset) and fades by ~9 degrees either side.
    const float day = SmoothStep(-4.0f, 9.0f, elevation);
    const float golden = Clamp01(1.0f - std::fabs(elevation) / 9.0f);

    // Blend night -> day, then fold the warm golden-hour tint over the result.
    Rgb zenith = Lerp(kNightZenith, kDayZenith, day);
    Rgb horizon = Lerp(kNightHorizon, kDayHorizon, day);
    Rgb ground = Lerp(kNightGround, kDayGround, day);
    Rgb ambient = Lerp(kNightAmbient, kDayAmbient, day);

    const float goldenTint = golden * 0.85f;
    zenith = Lerp(zenith, kGoldenZenith, goldenTint * 0.5f);
    horizon = Lerp(horizon, kGoldenHorizon, goldenTint);
    ground = Lerp(ground, kGoldenGround, goldenTint * 0.6f);
    ambient = Lerp(ambient, kGoldenAmbient, goldenTint * 0.7f);

    SkyEnvironment& sky = settings.sky;
    sky.mode = SkyMode::Procedural;
    sky.zenithColor = Pack(zenith);
    sky.horizonColor = Pack(horizon);
    sky.groundColor = Pack(ground);
    sky.ambientColor = Pack(ambient);
    sky.intensity = 0.12f + 0.78f * day;
    sky.ambientIntensity = 0.06f + 0.42f * day;
    sky.nightAmbientIntensity = 0.03f;
    sky.sunDisk = true;

    // Fire clouds: at golden hour the cloud layer glows warm. Below that the
    // cloud color reverts to the physical model so daytime clouds stay neutral.
    VolumetricCloudSettings& clouds = settings.clouds;
    if (m_config.fireCloudsAtGoldenHour && golden > 0.15f) {
        clouds.colorMode = CloudColorMode::Fire;
        clouds.litColor = Pack(kFireLit);
        clouds.fireColor = Pack(kFireCore);
        clouds.fireEmission = 1.5f + 3.5f * golden;
    } else {
        clouds.colorMode = CloudColorMode::Lit;
    }
}

} // namespace Concord
