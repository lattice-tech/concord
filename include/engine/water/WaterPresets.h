#ifndef CONCORD_WATERPRESETS_H
#define CONCORD_WATERPRESETS_H

#include "Concord/CExport.h"
#include "engine/water/WaterSurfaceDesc.h"

namespace Concord::Water {

/**
 * @brief Authored starting points for the water bodies a game usually needs.
 *
 * Each returns a complete WaterSurfaceDesc the caller can spawn as-is or adjust
 * field by field. They exist so picking "a lake" or "a flowing river" is one
 * call instead of a dozen tuned numbers, and so the tuning that makes each read
 * correctly lives in one reviewable place rather than in every game's setup code.
 */
namespace Presets {

/** A mirror-flat pond: no waves, no current, planar reflection on. */
CENGINE_API WaterSurfaceDesc StillLake(float width = 40.0f, float length = 40.0f) noexcept;

/** An open lake with a long swell and crossing chop, no net current. */
CENGINE_API WaterSurfaceDesc WavyLake(float width = 60.0f, float length = 60.0f) noexcept;

/**
 * A calm river: the channel is still, but shallow and clear, so its bed shows
 * through. Use it for a canal, a reflecting channel, or a stylised look.
 */
CENGINE_API WaterSurfaceDesc StaticRiver(float width = 8.0f, float length = 60.0f) noexcept;

/**
 * A flowing river: a current along the channel's length plus short, steep chop
 * riding it, shallow enough to stay clear and foam along its banks.
 */
CENGINE_API WaterSurfaceDesc FlowingRiver(float width = 8.0f, float length = 60.0f) noexcept;

/**
 * An open-ocean surface: wind-driven spectrum (Pierson–Moskowitz significant
 * height from the authored wind speed), broad directional spread, deep
 * absorption and strong sub-surface scattering. The wind rather than the
 * Gerstner fields drives the waves, and the planar reflection is off by
 * default (a whole-ocean mirror is rarely worth the cost); the analytic sky
 * reflection carries the surface instead.
 */
CENGINE_API WaterSurfaceDesc OpenOcean(float width = 800.0f, float length = 800.0f) noexcept;

} // namespace Presets

} // namespace Concord::Water

#endif // CONCORD_WATERPRESETS_H
