#include "engine/object/light/SunLight.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Concord::Object {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegreesToRadians = kPi / 180.0;
constexpr double kRadiansToDegrees = 180.0 / kPi;
constexpr double kSunriseElevationDegrees = -0.833;

bool IsLeapYear(int year) noexcept
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

int DaysInMonth(int year, int month) noexcept
{
    constexpr int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month == 2 && IsLeapYear(year) ? 29 : days[month - 1];
}

int CalendarDayOfYear(int year, int month, int day) noexcept
{
    int result = day;
    for (int currentMonth = 1; currentMonth < month; ++currentMonth) {
        result += DaysInMonth(year, currentMonth);
    }
    return result;
}

float NormalizeHours(float hours) noexcept
{
    if (!std::isfinite(hours)) {
        return 12.0f;
    }
    float normalized = std::fmod(hours, 24.0f);
    if (normalized < 0.0f) {
        normalized += 24.0f;
    }
    return normalized;
}

float NormalizeDegrees(float degrees) noexcept
{
    if (!std::isfinite(degrees)) {
        return 0.0f;
    }
    float normalized = std::fmod(degrees, 360.0f);
    if (normalized < 0.0f) {
        normalized += 360.0f;
    }
    return normalized;
}

float ClampLatitude(float degrees) noexcept
{
    if (!std::isfinite(degrees)) {
        return 0.0f;
    }
    return std::clamp(degrees, -90.0f, 90.0f);
}

float ClampIntensity(float intensity) noexcept
{
    return std::isfinite(intensity) && intensity > 0.0f ? intensity : 0.0f;
}

double SmoothStep(double edge0, double edge1, double value) noexcept
{
    const double t = std::clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

std::uint32_t ToColorByte(double value) noexcept
{
    return static_cast<std::uint32_t>(
        std::lround(std::clamp(value, 0.0, 255.0)));
}

std::uint32_t ColorFromTemperature(float kelvin) noexcept
{
    const double temperature = std::clamp(static_cast<double>(kelvin), 1000.0, 40000.0)
        / 100.0;

    const double red = temperature <= 66.0
        ? 255.0
        : 329.698727446 * std::pow(temperature - 60.0, -0.1332047592);
    const double green = temperature <= 66.0
        ? 99.4708025861 * std::log(temperature) - 161.1195681661
        : 288.1221695283 * std::pow(temperature - 60.0, -0.0755148492);
    double blue = 255.0;
    if (temperature <= 19.0) {
        blue = 0.0;
    } else if (temperature < 66.0) {
        blue = 138.5177312231 * std::log(temperature - 10.0) - 305.0447927307;
    }

    return (ToColorByte(red) << 24)
        | (ToColorByte(green) << 16)
        | (ToColorByte(blue) << 8)
        | 0xffu;
}

} // namespace

SunLightDesc SanitizeSunLightDesc(SunLightDesc desc) noexcept
{
    if (desc.timeMode != SunTimeMode::ApparentSolar
        && desc.timeMode != SunTimeMode::Civil
        && desc.timeMode != SunTimeMode::Manual) {
        desc.timeMode = SunTimeMode::ApparentSolar;
    }
    desc.localSolarTimeHours = NormalizeHours(desc.localSolarTimeHours);
    desc.civilTimeHours = NormalizeHours(desc.civilTimeHours);
    desc.latitudeDegrees = ClampLatitude(desc.latitudeDegrees);
    desc.longitudeDegrees = std::clamp(
        std::isfinite(desc.longitudeDegrees) ? desc.longitudeDegrees : 0.0f,
        -180.0f, 180.0f);
    desc.timeZoneHours = std::clamp(
        std::isfinite(desc.timeZoneHours) ? desc.timeZoneHours : 0.0f,
        -14.0f, 14.0f);
    desc.dayOfYear = std::clamp(desc.dayOfYear, 1, 365);
    desc.year = std::clamp(desc.year, 1, 9999);
    desc.month = std::clamp(desc.month, 1, 12);
    desc.day = std::clamp(desc.day, 1, DaysInMonth(desc.year, desc.month));
    desc.manualElevationDegrees = std::clamp(
        std::isfinite(desc.manualElevationDegrees) ? desc.manualElevationDegrees : 45.0f,
        -90.0f, 90.0f);
    desc.manualAzimuthDegrees = NormalizeDegrees(desc.manualAzimuthDegrees);
    desc.northYawDegrees = NormalizeDegrees(desc.northYawDegrees);
    desc.maximumIntensity = ClampIntensity(desc.maximumIntensity);
    desc.turbidity = std::clamp(
        std::isfinite(desc.turbidity) ? desc.turbidity : 2.0f, 1.0f, 20.0f);
    desc.overrideIntensity = ClampIntensity(desc.overrideIntensity);
    desc.directionalAngularRadiusDegrees = std::clamp(
        std::isfinite(desc.directionalAngularRadiusDegrees)
            ? desc.directionalAngularRadiusDegrees
            : kDefaultDirectionalAngularRadiusDegrees,
        0.0f, 45.0f);
    desc.visibleDiskIntensity = ClampIntensity(desc.visibleDiskIntensity);
    return desc;
}

SunLight::SunLight(SunLightDesc desc)
    : Light()
    , m_desc(SanitizeSunLightDesc(desc))
{
    m_localSolarTimeHours = m_desc.localSolarTimeHours;
    m_latitudeDegrees = m_desc.latitudeDegrees;
    m_dayOfYear = m_desc.dayOfYear;
    m_northYawDegrees = m_desc.northYawDegrees;
    m_maximumIntensity = m_desc.maximumIntensity;
    SetLocalTransform(m_desc.transform);
    Light::SetType(LightType::Directional);
    Light::SetCastShadow(m_desc.castShadow);
    Light::SetDirectionalAngularRadiusDegrees(m_desc.directionalAngularRadiusDegrees);
    SetSunAppearance(true, m_desc.visibleDisk, m_desc.visibleDiskIntensity);
    RefreshSolarState();
}

SunLightState SunLight::CalculateState(const SunLightDesc& desc) noexcept
{
    const SunLightDesc clean = SanitizeSunLightDesc(desc);
    double solarTime = static_cast<double>(clean.localSolarTimeHours);
    int dayOfYear = clean.dayOfYear;
    if (clean.timeMode == SunTimeMode::Civil) {
        dayOfYear = CalendarDayOfYear(clean.year, clean.month, clean.day);
        const double annualAngle = 2.0 * kPi / 365.0
            * (static_cast<double>(dayOfYear) - 1.0
               + (static_cast<double>(clean.civilTimeHours) - 12.0) / 24.0);
        const double equationMinutes = 229.18
            * (0.000075 + 0.001868 * std::cos(annualAngle)
               - 0.032077 * std::sin(annualAngle)
               - 0.014615 * std::cos(2.0 * annualAngle)
               - 0.040849 * std::sin(2.0 * annualAngle));
        solarTime = static_cast<double>(clean.civilTimeHours)
            + equationMinutes / 60.0
            + static_cast<double>(clean.longitudeDegrees) / 15.0
            - static_cast<double>(clean.timeZoneHours);
    }
    const double latitude = static_cast<double>(clean.latitudeDegrees)
        * kDegreesToRadians;
    const double northYaw = static_cast<double>(clean.northYawDegrees)
        * kDegreesToRadians;

    // Cooper's compact annual declination approximation, phase-aligned so
    // day 80 is the nominal March equinox.
    const double declinationDegrees = 23.44
        * std::sin(2.0 * kPi * static_cast<double>(dayOfYear - 80) / 365.0);
    const double declination = declinationDegrees * kDegreesToRadians;
    const double hourAngle = (solarTime - 12.0) * 15.0 * kDegreesToRadians;

    const double sinLatitude = std::sin(latitude);
    const double cosLatitude = std::cos(latitude);
    const double sinDeclination = std::sin(declination);
    const double cosDeclination = std::cos(declination);
    const double sinHourAngle = std::sin(hourAngle);
    const double cosHourAngle = std::cos(hourAngle);

    double east = -cosDeclination * sinHourAngle;
    double north = cosLatitude * sinDeclination
        - sinLatitude * cosDeclination * cosHourAngle;
    double up = std::clamp(
        sinLatitude * sinDeclination + cosLatitude * cosDeclination * cosHourAngle,
        -1.0, 1.0);
    if (clean.timeMode == SunTimeMode::Manual) {
        const double elevation = static_cast<double>(clean.manualElevationDegrees)
            * kDegreesToRadians;
        const double azimuth = static_cast<double>(clean.manualAzimuthDegrees)
            * kDegreesToRadians;
        up = std::sin(elevation);
        const double horizontal = std::cos(elevation);
        east = horizontal * std::sin(azimuth);
        north = horizontal * std::cos(azimuth);
    }

    const double worldToSunX = east * std::cos(northYaw) + north * std::sin(northYaw);
    const double worldToSunZ = -east * std::sin(northYaw) + north * std::cos(northYaw);
    const double directionLength = std::sqrt(
        worldToSunX * worldToSunX + up * up + worldToSunZ * worldToSunZ);
    const double inverseLength = directionLength > 1e-12 ? 1.0 / directionLength : 1.0;

    SunLightState state;
    state.direction = {
        static_cast<float>(-worldToSunX * inverseLength),
        static_cast<float>(-up * inverseLength),
        static_cast<float>(-worldToSunZ * inverseLength),
    };
    state.elevationDegrees = static_cast<float>(std::asin(up) * kRadiansToDegrees);
    state.azimuthDegrees = NormalizeDegrees(static_cast<float>(
        std::atan2(east, north) * kRadiansToDegrees));

    const double daylightOnset = SmoothStep(
        kSunriseElevationDegrees, 3.0, state.elevationDegrees);
    const double elevationEnergy = up > 0.0 ? std::pow(up, 0.35) : 0.0;
    state.daylightFactor = static_cast<float>(
        std::clamp(daylightOnset * elevationEnergy, 0.0, 1.0));
    const double turbidityAttenuation = std::exp(
        -std::max(static_cast<double>(clean.turbidity) - 2.0, 0.0) * 0.025);
    state.intensity = clean.overrideIntensityEnabled
        ? clean.overrideIntensity
        : clean.maximumIntensity * state.daylightFactor
            * static_cast<float>(turbidityAttenuation);

    const double temperatureBlend = SmoothStep(
        kSunriseElevationDegrees, 35.0, state.elevationDegrees);
    state.colorTemperatureKelvin = static_cast<float>(std::clamp(
        2000.0 + (6500.0 - 2000.0) * temperatureBlend
            - (static_cast<double>(clean.turbidity) - 2.0) * 120.0,
        1000.0, 40000.0));
    state.color = clean.overrideColorEnabled
        ? clean.overrideColor : ColorFromTemperature(state.colorTemperatureKelvin);
    return state;
}

void SunLight::SetLocalSolarTimeHours(float hours) noexcept
{
    m_localSolarTimeHours = NormalizeHours(hours);
    m_desc.localSolarTimeHours = m_localSolarTimeHours;
    RefreshSolarState();
}

void SunLight::SetTimeMode(SunTimeMode mode) noexcept
{
    m_desc.timeMode = mode;
    m_desc = SanitizeSunLightDesc(m_desc);
    RefreshSolarState();
}

void SunLight::SetCivilTimeHours(float hours) noexcept
{
    m_desc.civilTimeHours = NormalizeHours(hours);
    RefreshSolarState();
}

void SunLight::SetCivilLocation(float longitudeDegrees, float timeZoneHours) noexcept
{
    m_desc.longitudeDegrees = longitudeDegrees;
    m_desc.timeZoneHours = timeZoneHours;
    m_desc = SanitizeSunLightDesc(m_desc);
    RefreshSolarState();
}

void SunLight::SetDate(int year, int month, int day) noexcept
{
    m_desc.year = year;
    m_desc.month = month;
    m_desc.day = day;
    m_desc = SanitizeSunLightDesc(m_desc);
    RefreshSolarState();
}

void SunLight::SetManualSunAngles(float elevationDegrees, float azimuthDegrees) noexcept
{
    m_desc.manualElevationDegrees = elevationDegrees;
    m_desc.manualAzimuthDegrees = azimuthDegrees;
    m_desc = SanitizeSunLightDesc(m_desc);
    RefreshSolarState();
}

void SunLight::SetLatitudeDegrees(float latitudeDegrees) noexcept
{
    m_latitudeDegrees = ClampLatitude(latitudeDegrees);
    m_desc.latitudeDegrees = m_latitudeDegrees;
    RefreshSolarState();
}

void SunLight::SetDayOfYear(int dayOfYear) noexcept
{
    m_dayOfYear = std::clamp(dayOfYear, 1, 365);
    m_desc.dayOfYear = m_dayOfYear;
    RefreshSolarState();
}

void SunLight::SetNorthYawDegrees(float northYawDegrees) noexcept
{
    m_northYawDegrees = NormalizeDegrees(northYawDegrees);
    m_desc.northYawDegrees = m_northYawDegrees;
    RefreshSolarState();
}

void SunLight::SetMaximumIntensity(float maximumIntensity) noexcept
{
    m_maximumIntensity = ClampIntensity(maximumIntensity);
    m_desc.maximumIntensity = m_maximumIntensity;
    RefreshSolarState();
}

void SunLight::SetTurbidity(float turbidity) noexcept
{
    m_desc.turbidity = turbidity;
    m_desc = SanitizeSunLightDesc(m_desc);
    RefreshSolarState();
}

void SunLight::SetColorOverride(bool enabled, std::uint32_t color) noexcept
{
    m_desc.overrideColorEnabled = enabled;
    m_desc.overrideColor = color;
    RefreshSolarState();
}

void SunLight::SetIntensityOverride(bool enabled, float intensity) noexcept
{
    m_desc.overrideIntensityEnabled = enabled;
    m_desc.overrideIntensity = ClampIntensity(intensity);
    RefreshSolarState();
}

void SunLight::SetVisibleDisk(bool visible, float intensity) noexcept
{
    m_desc.visibleDisk = visible;
    m_desc.visibleDiskIntensity = ClampIntensity(intensity);
    SetSunAppearance(true, m_desc.visibleDisk, m_desc.visibleDiskIntensity);
}

void SunLight::RefreshSolarState() noexcept
{
    m_state = CalculateState(m_desc);

    Light::SetDirection(m_state.direction);
    Light::SetColor(m_state.color);
    Light::SetIntensity(m_state.intensity);
}

} // namespace Concord::Object
