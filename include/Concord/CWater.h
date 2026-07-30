#ifndef CONCORD_CWATER_H
#define CONCORD_CWATER_H

/**
 * Public entry point for water bodies: lakes and rivers, still or flowing.
 *
 * This header only re-exports the real declarations from the engine module's
 * private headers (see AGENTS.md §3, facade re-export pattern); application code
 * includes this file, never the ones under `engine/`.
 */

#include "engine/object/water/WaterBody.h"
#include "engine/water/WaterPresets.h"
#include "engine/water/WaterSurfaceDesc.h"
#include "engine/water/WaterWave.h"

#endif // CONCORD_CWATER_H
