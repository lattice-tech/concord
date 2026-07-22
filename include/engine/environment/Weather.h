#ifndef CONCORD_WEATHER_H
#define CONCORD_WEATHER_H

#include "Concord/CExport.h"
#include "engine/environment/EnvironmentSettings.h"

#include <cstdint>

namespace Concord {

/** Named coarse weather conditions that preset the cloud and fog controls. */
enum class WeatherCondition : std::uint8_t {
    Clear = 0,
    PartlyCloudy,
    Overcast,
    Storm,
};

/**
 * A thin interface over the environment's cloud/fog data for weather authoring.
 *
 * This is intentionally interface-only: it maps high-level weather intent onto
 * the existing EnvironmentSettings fields (cloud coverage/density, fog, and a
 * stored precipitation amount). It does NOT render precipitation — rain and
 * snow particle/volume rendering are a separate, unimplemented feature. Callers
 * (or a future game weather system) drive these setters; a renderer can later
 * read `clouds.precipitation` to add rain without changing this API.
 */
class CENGINE_API Weather {
public:
    /** Presets cloud coverage/density/precipitation and fog for @p condition. */
    static void Apply(EnvironmentSettings& settings, WeatherCondition condition) noexcept;

    /**
     * Stores rain/snow intensity in [0, 1] on the cloud precipitation field and
     * darkens/thickens clouds accordingly. Rendering of the precipitation itself
     * is not implemented; this only exposes the authoring interface.
     */
    static void SetRainIntensity(EnvironmentSettings& settings, float intensity) noexcept;

    /** Sets cloud coverage in [0, 1]. */
    static void SetCoverage(EnvironmentSettings& settings, float coverage) noexcept;

    /** Sets horizontal wind direction (degrees from north) and speed (km/s). */
    static void SetWind(EnvironmentSettings& settings, float directionDegrees,
                        float speedKmPerSecond) noexcept;
};

} // namespace Concord

#endif // CONCORD_WEATHER_H
