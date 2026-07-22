#ifndef CONCORD_CSCENE_H
#define CONCORD_CSCENE_H

#include "engine/scene/io/SceneIO.h"

/**
 * Public entry point for the scene system (CEngine.dll).
 *
 * This header only re-exports the real declarations from the engine module's
 * private headers (see AGENTS.md §3, facade re-export pattern); application
 * code includes this file to reach Concord::Scene, never the headers under
 * `engine/`.
 */

#include "engine/scene/Scene.h"

#endif // CONCORD_CSCENE_H
