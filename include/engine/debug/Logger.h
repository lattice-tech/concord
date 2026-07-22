#ifndef CONCORD_LOGGER_H
#define CONCORD_LOGGER_H

#include "Concord/CExport.h"
#include "engine/debug/LogLevel.h"

namespace Concord::Debug {

/**
 * The engine's process-wide, thread-safe diagnostic logger.
 *
 * A single severity threshold (SetLevel) gates every record; the runtime
 * mode seeds it (see ConfigureFromMode), so `mode=Debug` in the config file
 * turns on the Debug/Trace chatter and `mode=Release` keeps only Info and up.
 * Records are written to stderr with a `[LEVEL][category]` prefix and a
 * trailing newline, formatted printf-style.
 *
 * Every engine subsystem shares this one logger rather than each reinventing
 * std::printf, so a single switch controls all diagnostics and every line is
 * tagged consistently. Both the main thread and the render thread log, so all
 * entry points are synchronized internally.
 */
class CENGINE_API Logger {
public:
    /** Sets the minimum severity that will be emitted (records below are dropped). */
    static void SetLevel(LogLevel level) noexcept;

    /** The current severity threshold. */
    static LogLevel Level() noexcept;

    /** True when a record at `level` would currently be emitted. */
    static bool IsEnabled(LogLevel level) noexcept;

    /** Convenience: lowers the threshold to Debug in Debug mode, Info in Release. */
    static void ConfigureForDebug(bool debugMode) noexcept;

    /**
     * Emits one printf-style record under `category` at `level`, if enabled.
     * @param category short subsystem tag, e.g. "Render" or "Config".
     */
    static void Write(LogLevel level, const char* category, const char* format, ...);

    /** Shorthands for Write at a fixed level. */
    static void Trace(const char* category, const char* format, ...);
    static void Debug(const char* category, const char* format, ...);
    static void Info(const char* category, const char* format, ...);
    static void Warn(const char* category, const char* format, ...);
    static void Error(const char* category, const char* format, ...);
};

} // namespace Concord::Debug

#endif // CONCORD_LOGGER_H
