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
 * Maps high-level weather conditions to renderer-backed cloud and fog values.
 */
class CENGINE_API Weather {
public:
    /** Presets cloud coverage, density, and fog for @p condition. */
    static void Apply(EnvironmentSettings& settings, WeatherCondition condition) noexcept;

    /** Sets cloud coverage in [0, 1]. */
    static void SetCoverage(EnvironmentSettings& settings, float coverage) noexcept;

    /** Sets horizontal wind direction (degrees from north) and speed (km/s). */
    static void SetWind(EnvironmentSettings& settings, float directionDegrees,
                        float speedKmPerSecond) noexcept;
};

} // namespace Concord

#endif // CONCORD_WEATHER_H
