#ifndef CONCORD_CENV_H
#define CONCORD_CENV_H

/**
 * Public entry point for the global environment variables.
 *
 * This header only re-exports the real declarations from the engine
 * module's private headers (see AGENTS.md §3, facade re-export pattern);
 * application code includes this file to reach Concord::Env, never the
 * headers under `engine/`.
 */

#include "engine/env/Env.h"
#include "engine/env/EnvValue.h"

#endif // CONCORD_CENV_H
