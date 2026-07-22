#ifndef CONCORD_DAYNIGHTCYCLE_H
#define CONCORD_DAYNIGHTCYCLE_H

#include "Concord/CExport.h"
#include "engine/environment/EnvironmentSettings.h"

namespace Concord {

/** Authoring inputs for an automatic time-of-day cycle. */
struct DayNightConfig {
    /** Real seconds for one full 24-hour cycle. */
    float secondsPerDay = 120.0f;
    /** Civil clock hour the cycle starts at, in [0, 24). */
    float startHour = 8.0f;
    /** Multiplies the whole cycle speed; 0 pauses, negative runs time backward. */
    float timeScale = 1.0f;
    /** Enables the warm "fire cloud" tint around sunrise/sunset. */
    bool fireCloudsAtGoldenHour = true;
};

/**
 * A self-contained, engine-side driver for a realistic day-night cycle.
 *
 * It owns only a civil clock; it does not touch the Scene, a light, or bgfx, so
 * it stays pure and unit-testable. A caller advances it once per frame, feeds
 * the resulting civil hour to a Concord::Object::SunLight, then hands the sun's
 * resolved elevation back to ApplySky, which ramps the sky gradient, ambient
 * light, and cloud color between day, golden hour, and night. Keeping the color
 * ramp here (not in application code) is what makes it a reusable template.
 */
class CENGINE_API DayNightCycle {
public:
    /** Constructs the cycle at its configured start hour. */
    explicit DayNightCycle(DayNightConfig config = {}) noexcept;

    /** Advances the civil clock by @p realDeltaSeconds, wrapping across midnight. */
    void Advance(float realDeltaSeconds) noexcept;

    /** Current civil clock hour in [0, 24), to drive a SunLight. */
    float CivilHour() const noexcept { return m_civilHour; }

    /** Directly sets the civil hour, wrapping finite values into [0, 24). */
    void SetCivilHour(float hour) noexcept;

    /** Replaces the configuration, preserving the current clock. */
    void SetConfig(const DayNightConfig& config) noexcept { m_config = config; }

    /** Returns the current configuration. */
    const DayNightConfig& Config() const noexcept { return m_config; }

    /**
     * Ramps @p settings' sky, ambient, and cloud colors from the sun's apparent
     * elevation. Above the horizon reads as day; a band around zero degrees adds
     * a warm sunrise/sunset horizon and optional fire clouds; below reads as
     * night with a low cool ambient. Only visual color/intensity fields are
     * written; cloud shape, wind, fog geometry, and shadow controls are left
     * untouched so a weather system can own them independently.
     */
    void ApplySky(EnvironmentSettings& settings, float sunElevationDegrees) const noexcept;

private:
    DayNightConfig m_config{};
    float m_civilHour = 8.0f;
};

} // namespace Concord

#endif // CONCORD_DAYNIGHTCYCLE_H
