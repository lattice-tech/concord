#ifndef CONCORD_SUNLIGHT_H
#define CONCORD_SUNLIGHT_H

#include "Concord/CExport.h"
#include "engine/object/Light.h"
#include "engine/object/light/SunLightDesc.h"
#include "math/Vector3.h"

#include <cstdint>

namespace Concord::Object {

/** Resolved solar lighting values produced from a SunLightDesc. */
struct SunLightState {
    /** Local-space direction in which sunlight travels. */
    Vector3 direction{0.0f, -1.0f, 0.0f};

    /** Emitted sRGB color packed as 0xRRGGBBAA. */
    std::uint32_t color = 0xffffffffu;

    /** Engine radiant scale after solar-elevation attenuation. */
    float intensity = 0.0f;

    /** Apparent elevation above the horizon, in degrees. */
    float elevationDegrees = 0.0f;

    /** Apparent azimuth clockwise from geographic north, in degrees. */
    float azimuthDegrees = 0.0f;

    /** Approximate direct-sun correlated color temperature, in kelvin. */
    float colorTemperatureKelvin = 6500.0f;

    /** Normalized direct-daylight strength before maximumIntensity, in [0, 1]. */
    float daylightFactor = 0.0f;
};

/**
 * A directional light driven by local solar time and geographic latitude.
 *
 * The node computes solar declination from day-of-year, resolves the Sun's
 * local horizon direction, rotates geographic north into the authored world,
 * and derives warm low-angle color plus smooth daylight intensity. It remains
 * an Object::Light, so Scene collection and the existing PBR/shadow paths need
 * no special case.
 */
class CENGINE_API SunLight final : public Light {
public:
    /** Constructs a solar light and resolves its initial direction and output. */
    explicit SunLight(SunLightDesc desc = {});

    /**
     * Pure deterministic solar calculation used by the runtime and tests.
     * @param desc Geographic inputs and maximum engine intensity.
     * @return Resolved direction, color, intensity and diagnostic angles.
     */
    static SunLightState CalculateState(const SunLightDesc& desc) noexcept;

    /** Returns local solar time normalized to [0, 24). */
    float LocalSolarTimeHours() const noexcept { return m_localSolarTimeHours; }

    /** Returns the selected apparent, civil, or manual position mode. */
    SunTimeMode TimeMode() const noexcept { return m_desc.timeMode; }

    /** Returns local civil clock time normalized to [0, 24). */
    float CivilTimeHours() const noexcept { return m_desc.civilTimeHours; }

    /** Returns geographic latitude in degrees. */
    float LatitudeDegrees() const noexcept { return m_latitudeDegrees; }

    /** Returns longitude in degrees east. */
    float LongitudeDegrees() const noexcept { return m_desc.longitudeDegrees; }

    /** Returns the authored civil UTC offset in hours. */
    float TimeZoneHours() const noexcept { return m_desc.timeZoneHours; }

    /** Returns the clamped calendar day in [1, 365]. */
    int DayOfYear() const noexcept { return m_dayOfYear; }

    /** Returns geographic north yaw normalized to [0, 360). */
    float NorthYawDegrees() const noexcept { return m_northYawDegrees; }

    /** Returns the intensity an overhead Sun would emit. */
    float MaximumIntensity() const noexcept { return m_maximumIntensity; }

    /** Returns atmospheric aerosol turbidity. */
    float Turbidity() const noexcept { return m_desc.turbidity; }

    /** Returns whether an environment renderer may display the solar disk. */
    bool VisibleDisk() const noexcept { return m_desc.visibleDisk; }

    /** Returns the authored visible-disk luminance multiplier. */
    float VisibleDiskIntensity() const noexcept { return m_desc.visibleDiskIntensity; }

    /** Returns the latest resolved solar state. */
    const SunLightState& SolarState() const noexcept { return m_state; }

    /** Returns the latest apparent solar elevation in degrees. */
    float ElevationDegrees() const noexcept { return m_state.elevationDegrees; }

    /** Returns the latest apparent azimuth clockwise from north. */
    float AzimuthDegrees() const noexcept { return m_state.azimuthDegrees; }

    /** Returns the latest approximate direct-sun color temperature. */
    float ColorTemperatureKelvin() const noexcept
    {
        return m_state.colorTemperatureKelvin;
    }

    /** Returns normalized daylight strength in [0, 1]. */
    float DaylightFactor() const noexcept { return m_state.daylightFactor; }

    /** Sets local solar time, wrapping finite values into [0, 24). */
    void SetLocalSolarTimeHours(float hours) noexcept;

    /** Selects apparent-solar, civil-clock, or manual-angle positioning. */
    void SetTimeMode(SunTimeMode mode) noexcept;

    /** Sets local civil time, wrapping finite values into [0, 24). */
    void SetCivilTimeHours(float hours) noexcept;

    /** Sets longitude east and civil UTC offset. */
    void SetCivilLocation(float longitudeDegrees, float timeZoneHours) noexcept;

    /** Sets and validates the Gregorian date used by Civil mode. */
    void SetDate(int year, int month, int day) noexcept;

    /** Sets manual apparent elevation and azimuth. */
    void SetManualSunAngles(float elevationDegrees, float azimuthDegrees) noexcept;

    /** Sets geographic latitude, clamped to [-90, 90]. */
    void SetLatitudeDegrees(float latitudeDegrees) noexcept;

    /** Sets calendar day, clamped to [1, 365]. */
    void SetDayOfYear(int dayOfYear) noexcept;

    /** Sets node-local yaw of geographic north, normalized to [0, 360). */
    void SetNorthYawDegrees(float northYawDegrees) noexcept;

    /** Sets non-negative overhead engine intensity. */
    void SetMaximumIntensity(float maximumIntensity) noexcept;

    /** Sets aerosol turbidity in [1, 20]. */
    void SetTurbidity(float turbidity) noexcept;

    /** Enables or disables an explicit emitted color. */
    void SetColorOverride(bool enabled, std::uint32_t color = 0xffffffffu) noexcept;

    /** Enables or disables an explicit radiant intensity. */
    void SetIntensityOverride(bool enabled, float intensity = 5.0f) noexcept;

    /** Sets solar-disk visibility and a non-negative visible luminance scale. */
    void SetVisibleDisk(bool visible, float intensity = 1.0f) noexcept;

private:
    void RefreshSolarState() noexcept;

    using Light::SetColor;
    using Light::SetDirection;
    using Light::SetIntensity;
    using Light::SetType;

    float m_localSolarTimeHours = 12.0f;
    float m_latitudeDegrees = 35.0f;
    int m_dayOfYear = 172;
    float m_northYawDegrees = 0.0f;
    float m_maximumIntensity = 5.0f;
    SunLightDesc m_desc{};
    SunLightState m_state{};
};

} // namespace Concord::Object

#endif // CONCORD_SUNLIGHT_H
