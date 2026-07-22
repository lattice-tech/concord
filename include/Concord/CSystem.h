#ifndef CONCORD_CSYSTEM_H
#define CONCORD_CSYSTEM_H

/**
 * Public entry point for host hardware information (CSystem.dll).
 *
 * This header only re-exports the real declarations from the system module's
 * private headers (see AGENTS.md §3, facade re-export pattern); application
 * code includes this file to reach Concord::QueryHardwareInfo, never the
 * headers under `system/`.
 */

#include "system/HardwareInfo.h"

#endif // CONCORD_CSYSTEM_H
