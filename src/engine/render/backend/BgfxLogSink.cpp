#include "engine/render/backend/BgfxLogSink.h"

#include <cstdio>
#include <cstdarg>

namespace Concord::RenderDetail {

namespace {

BgfxLogSink g_logSink;

} // namespace

BgfxLogSink& LogSinkInstance()
{
    return g_logSink;
}

void BgfxLogSink::fatal(const char* filePath, std::uint16_t line, bgfx::Fatal::Enum code, const char* str)
{
    std::fprintf(stderr, "[bgfx] fatal (%d) at %s:%u: %s\n", static_cast<int>(code), filePath, line, str);
    std::fflush(stderr);
}

void BgfxLogSink::traceVargs(const char* filePath, std::uint16_t line, const char* format, va_list argList)
{
    std::fprintf(stderr, "[bgfx] %s:%u: ", filePath, line);
    std::vfprintf(stderr, format, argList);
    std::fflush(stderr);
}

} // namespace Concord::RenderDetail