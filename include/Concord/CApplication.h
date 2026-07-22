#ifndef CONCORD_CAPPLICATION_H
#define CONCORD_CAPPLICATION_H

/**
 * Public entry point for the engine lifecycle.
 *
 * This header only re-exports the real declarations from the engine
 * module's private headers (see AGENTS.md §3, facade re-export pattern);
 * application code includes this file, never the ones under `engine/`.
 */

#include "engine/app/ConfigLocator.h"
#include "engine/app/Game.h"
#include "engine/app/GameConfig.h"
#include "engine/render/postprocess/AntiAliasing.h"
#include "engine/window/Resolution.h"
#include "engine/window/Window.h"
#include "engine/window/WindowDesc.h"
#include "engine/window/WindowId.h"

#endif // CONCORD_CAPPLICATION_H
