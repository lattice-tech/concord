#ifndef CONCORD_CFLUID_H
#define CONCORD_CFLUID_H

/**
 * Public entry point for DFSPH fluid bodies: particle-simulated water tanks
 * with sparse Marching-Cubes surface reconstruction and dual-interface
 * refraction.
 *
 * This header only re-exports the real declarations from the engine module's
 * private headers (see AGENTS.md §3, facade re-export pattern); application
 * code includes this file, never the ones under `engine/`.
 */

#include "engine/fluid/FluidDesc.h"
#include "engine/object/fluid/FluidWater.h"

#endif // CONCORD_CFLUID_H
