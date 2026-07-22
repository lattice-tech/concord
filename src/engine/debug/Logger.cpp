#include "engine/debug/Logger.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace Concord::Debug {

namespace {

/** Threshold, atomic so IsEnabled() is a cheap lock-free fast path. */
std::atomic<LogLevel> g_level{LogLevel::Info};

/** Serializes the actual write so interleaved records never tear a line. */
std::mutex g_writeMutex;

/** Formats one already-enabled record and writes it as a single line. */
void Emit(LogLevel level, const char* category, const char* format, std::va_list args)
{
    char message[1024];
    const int written = std::vsnprintf(message, sizeof(message), format, args);
    if (written < 0) {
        message[0] = '\0';
    }

    std::lock_guard<std::mutex> lock(g_writeMutex);
    std::fprintf(stderr, "[%s][%s] %s\n",
                 ToString(level), category != nullptr ? category : "?", message);
    // Always flush so a crash right after cannot swallow the last record.
    std::fflush(stderr);
}

} // namespace

void Logger::SetLevel(LogLevel level) noexcept
{
    g_level.store(level, std::memory_order_relaxed);
}

LogLevel Logger::Level() noexcept
{
    return g_level.load(std::memory_order_relaxed);
}

bool Logger::IsEnabled(LogLevel level) noexcept
{
    return level >= Level() && level != LogLevel::Off;
}

void Logger::ConfigureForDebug(bool debugMode) noexcept
{
    SetLevel(debugMode ? LogLevel::Debug : LogLevel::Info);
}

void Logger::Write(LogLevel level, const char* category, const char* format, ...)
{
    if (!IsEnabled(level)) {
        return;
    }
    std::va_list args;
    va_start(args, format);
    Emit(level, category, format, args);
    va_end(args);
}

void Logger::Trace(const char* category, const char* format, ...)
{
    if (!IsEnabled(LogLevel::Trace)) {
        return;
    }
    std::va_list args;
    va_start(args, format);
    Emit(LogLevel::Trace, category, format, args);
    va_end(args);
}

void Logger::Debug(const char* category, const char* format, ...)
{
    if (!IsEnabled(LogLevel::Debug)) {
        return;
    }
    std::va_list args;
    va_start(args, format);
    Emit(LogLevel::Debug, category, format, args);
    va_end(args);
}

void Logger::Info(const char* category, const char* format, ...)
{
    if (!IsEnabled(LogLevel::Info)) {
        return;
    }
    std::va_list args;
    va_start(args, format);
    Emit(LogLevel::Info, category, format, args);
    va_end(args);
}

void Logger::Warn(const char* category, const char* format, ...)
{
    if (!IsEnabled(LogLevel::Warning)) {
        return;
    }
    std::va_list args;
    va_start(args, format);
    Emit(LogLevel::Warning, category, format, args);
    va_end(args);
}

void Logger::Error(const char* category, const char* format, ...)
{
    if (!IsEnabled(LogLevel::Error)) {
        return;
    }
    std::va_list args;
    va_start(args, format);
    Emit(LogLevel::Error, category, format, args);
    va_end(args);
}

} // namespace Concord::Debug
