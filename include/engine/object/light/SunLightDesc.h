#ifndef CONCORD_SUNLIGHTDESC_H
#define CONCORD_SUNLIGHTDESC_H

#include "Concord/CExport.h"
#include "engine/object/Transform.h"
#include "engine/render/frame/RenderLight.h"

#include <cstdint>

namespace Concord::Object {

/** Selects how SunLight resolves its apparent position. */
enum class SunTimeMode : std::uint8_t {
    /** Preserve the original local apparent solar-time authoring contract. */
    ApparentSolar = 0,
    /** Convert local civil clock time using date, longitude, and time zone. */
    Civil,
    /** Use explicitly authored elevation and azimuth angles. */
    Manual,
};

/**
 * Geographic and authoring inputs for a physically directed SunLight.
 *
 * Local solar time intentionally omits time zones and the equation of time:
 * noon is exactly the instant the Sun crosses the local meridian. This keeps
 * the runtime contract deterministic while allowing a calendar or weather
 * system to perform civil-time conversion before setting the value.
 */
struct SunLightDesc {
    /** Optional scene-graph transform applied after the geographic direction. */
    Transform transform{};

    /** Position/time authoring mode. */
    SunTimeMode timeMode = SunTimeMode::ApparentSolar;

    /** Local apparent solar time in hours; values wrap into [0, 24). */
    float localSolarTimeHours = 12.0f;

    /** Local civil clock time in hours; values wrap into [0, 24). */
    float civilTimeHours = 12.0f;

    /** Geographic latitude in degrees, clamped to [-90, 90]. */
    float latitudeDegrees = 35.0f;

    /** Geographic longitude in degrees east, clamped to [-180, 180]. */
    float longitudeDegrees = 0.0f;

    /** Civil UTC offset in hours, including any authored daylight adjustment. */
    float timeZoneHours = 0.0f;

    /** Calendar day in [1, 365]; leap-day callers should map day 366 to 365. */
    int dayOfYear = 172;

    /** Gregorian calendar year used by Civil mode. */
    int year = 2024;

    /** Gregorian calendar month in [1, 12], used by Civil mode. */
    int month = 6;

    /** Gregorian day of month, clamped to the selected month. */
    int day = 20;

    /** Manual-mode apparent elevation in degrees. */
    float manualElevationDegrees = 45.0f;

    /** Manual-mode azimuth clockwise from geographic north. */
    float manualAzimuthDegrees = 180.0f;

    /**
     * Node-local yaw of geographic north in degrees. Zero maps north to +Z;
     * positive yaw rotates north toward +X before the node transform applies.
     */
    float northYawDegrees = 0.0f;

    /** Engine light intensity reached by an overhead Sun. */
    float maximumIntensity = 5.0f;

    /** Atmospheric aerosol turbidity in [1, 20]. */
    float turbidity = 2.0f;

    /** Uses overrideColor instead of the atmospheric color estimate. */
    bool overrideColorEnabled = false;

    /** Explicit emitted color packed as 0xRRGGBBAA in sRGB. */
    std::uint32_t overrideColor = 0xffffffffu;

    /** Uses overrideIntensity without solar-elevation attenuation. */
    bool overrideIntensityEnabled = false;

    /** Explicit non-negative engine radiant scale. */
    float overrideIntensity = 5.0f;

    /** Apparent solar-disc radius used by contact-hardening shadows. */
    float directionalAngularRadiusDegrees = kDefaultDirectionalAngularRadiusDegrees;

    /** Whether environment rendering may display the solar disk. */
    bool visibleDisk = true;

    /** Visible-disk luminance multiplier independent of direct lighting. */
    float visibleDiskIntensity = 1.0f;

    /** Whether the Sun participates in the directional shadow pass. */
    bool castShadow = true;
};

/** Returns finite, normalized, calendar-valid sunlight authoring inputs. */
CENGINE_API SunLightDesc SanitizeSunLightDesc(SunLightDesc desc) noexcept;

} // namespace Concord::Object

#endif // CONCORD_SUNLIGHTDESC_H
