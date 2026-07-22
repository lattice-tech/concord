#ifndef CONCORD_CCOLOR_H
#define CONCORD_CCOLOR_H

/**
 * Public entry point for the named color macros.
 *
 * Like <Concord/CMath.h>, there is no backing module or DLL here: colors are
 * plain packed-integer constants, defined in their header under `color/`,
 * and this file only re-exports them so application code has a single
 * `#include <Concord/CColor.h>` entry point (see AGENTS.md §3).
 */

#include "color/Color.h"

#endif // CONCORD_CCOLOR_H
