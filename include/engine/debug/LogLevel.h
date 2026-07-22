#ifndef CONCORD_LOGLEVEL_H
#define CONCORD_LOGLEVEL_H

namespace Concord::Debug {

/**
 * Severity of a log record, ordered from most to least verbose.
 *
 * The logger keeps a single threshold (see Logger::SetLevel): a record is
 * emitted only when its level is at least as severe as the threshold. The
 * runtime mode seeds that threshold — Debug builds lower it to Trace/Debug so
 * diagnostic chatter appears, Release raises it to Info so only meaningful
 * events do. `Off` is a threshold only; no record is ever tagged with it.
 */
enum class LogLevel {
    /** Fine-grained tracing: per-frame or per-call spam, Debug builds only. */
    Trace,

    /** Developer diagnostics that help explain engine behavior. */
    Debug,

    /** Normal lifecycle milestones (subsystem up, config loaded, ...). */
    Info,

    /** A recoverable problem the caller should know about. */
    Warning,

    /** A failure that prevented an operation from completing. */
    Error,

    /** Threshold sentinel that silences every level. */
    Off,
};

/** Canonical, fixed-width tag for a level (never null); used in log prefixes. */
inline const char* ToString(LogLevel level) noexcept
{
    switch (level) {
        case LogLevel::Trace:   return "TRACE";
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO ";
        case LogLevel::Warning: return "WARN ";
        case LogLevel::Error:   return "ERROR";
        case LogLevel::Off:     return "OFF  ";
    }
    return "INFO ";
}

} // namespace Concord::Debug

#endif // CONCORD_LOGLEVEL_H
