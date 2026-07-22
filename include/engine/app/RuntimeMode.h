#ifndef CONCORD_RUNTIMEMODE_H
#define CONCORD_RUNTIMEMODE_H

namespace Concord {

/**
 * Coarse runtime profile the engine starts in.
 *
 * Debug favors validation and diagnostics (extra checks, verbose logging),
 * Release favors throughput. The flag is recorded from the config now;
 * subsystems consult it as they gain debug-only behavior.
 */
enum class RuntimeMode {
    Debug,
    Release,
};

/** Canonical, human-readable name of a RuntimeMode (never null). */
inline const char* ToString(RuntimeMode mode)
{
    switch (mode) {
        case RuntimeMode::Debug:   return "Debug";
        case RuntimeMode::Release: return "Release";
    }
    return "Debug";
}

} // namespace Concord

#endif // CONCORD_RUNTIMEMODE_H
