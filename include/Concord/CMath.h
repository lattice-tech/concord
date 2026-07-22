#ifndef CONCORD_CMATH_H
#define CONCORD_CMATH_H

/**
 * Public entry point for Concord's small math types.
 *
 * Unlike the other `C*.h` facades, there is no backing module/DLL here:
 * Vector3, Quaternion and EulerAngles are plain, dependency-free
 * aggregates, so they are defined directly in their headers under
 * `include/math/` and this file only re-exports them, matching the facade
 * re-export pattern (see AGENTS.md §3) so application code has a single
 * `#include <Concord/CMath.h>` entry point regardless.
 */

#include "math/EulerAngles.h"
#include "math/Quaternion.h"
#include "math/Vector3.h"

#endif // CONCORD_CMATH_H
