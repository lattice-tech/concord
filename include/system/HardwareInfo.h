#ifndef CONCORD_HARDWAREINFO_H
#define CONCORD_HARDWAREINFO_H

#include "Concord/CExport.h"

#include <string>

namespace Concord {

/**
 * A snapshot of the host machine's CPU and memory characteristics (CSystem.dll).
 *
 * Populated by QueryHardwareInfo from the platform layer. It describes only
 * facts that are available without bringing up any graphics device, so it can
 * be queried before (or without) ever creating a Game — useful for logging a
 * run's environment, picking default quality settings, or gating features on
 * a required instruction set. GPU/renderer details are deliberately absent
 * here since they only exist once a render backend is up.
 */
struct HardwareInfo {
    /** Platform name, e.g. "Windows", "Linux", "macOS". */
    std::string platform;

    /** Number of logical CPU cores (hardware threads). */
    int logicalCores = 0;

    /** Total usable system RAM, in megabytes. */
    int systemRamMB = 0;

    /** L1 cache line size in bytes, handy for alignment decisions. */
    int cpuCacheLineSize = 0;

    /** Whether the CPU advertises the SSE4.2 instruction set. */
    bool hasSSE42 = false;

    /** Whether the CPU advertises the AVX instruction set. */
    bool hasAVX = false;

    /** Whether the CPU advertises the AVX2 instruction set. */
    bool hasAVX2 = false;

    /** Whether the CPU advertises the ARM NEON instruction set. */
    bool hasNEON = false;
};

/**
 * Queries the host machine's hardware characteristics.
 *
 * Safe to call at any time, including before any engine subsystem starts; it
 * touches only lightweight platform queries and never initializes SDL.
 */
CSYSTEM_API HardwareInfo QueryHardwareInfo();

} // namespace Concord

#endif // CONCORD_HARDWAREINFO_H
