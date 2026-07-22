#ifndef CONCORD_CINPUT_H
#define CONCORD_CINPUT_H

/**
 * Public entry point for keyboard and mouse input (CEngine.dll).
 *
 * This header only re-exports the real declarations from the engine module's
 * private headers (see AGENTS.md §3, facade re-export pattern); application
 * code includes this file to reach Concord::Input, Concord::Key and
 * Concord::MouseButton, never the headers under `engine/`.
 */

#include "engine/input/Input.h"
#include "engine/input/Key.h"
#include "engine/input/MouseButton.h"
#include "engine/input/action/ActionId.h"
#include "engine/input/action/AxisId.h"
#include "engine/input/action/InputActions.h"
#include "engine/input/action/InputBinding.h"
#include "engine/input/action/InputContext.h"
#include "engine/input/action/InputContextPriority.h"

#endif // CONCORD_CINPUT_H
