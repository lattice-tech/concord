#ifndef CONCORD_BGFXLOGSINK_H
#define CONCORD_BGFXLOGSINK_H

#include <bgfx/bgfx.h>

#include <cstdint>

namespace Concord::RenderDetail {

/**
 * Forwards bgfx's own diagnostics to stderr.
 *
 * Without a callback, bgfx only logs through platform-specific debug output
 * (e.g. OutputDebugString), which is invisible outside a debugger; this makes
 * `bgfx::init` failures and fatal errors actually diagnosable on a running
 * process. The single shared instance lives in the matching .cpp; the engine
 * mounts it once through `bgfx::Init::callback` during backend initialization.
 */
class BgfxLogSink final : public bgfx::CallbackI {
public:
    void fatal(const char* filePath, std::uint16_t line, bgfx::Fatal::Enum code, const char* str) override;
    void traceVargs(const char* filePath, std::uint16_t line, const char* format, va_list argList) override;
    void profilerBegin(const char*, std::uint32_t, const char*, std::uint16_t) override {}
    void profilerBeginLiteral(const char*, std::uint32_t, const char*, std::uint16_t) override {}
    void profilerEnd() override {}
    std::uint32_t cacheReadSize(std::uint64_t) override { return 0; }
    bool cacheRead(std::uint64_t, void*, std::uint32_t) override { return false; }
    void cacheWrite(std::uint64_t, const void*, std::uint32_t) override {}
    void screenShot(const char*, std::uint32_t, std::uint32_t, std::uint32_t, bgfx::TextureFormat::Enum,
                    const void*, std::uint32_t, bool) override {}
    void captureBegin(std::uint32_t, std::uint32_t, std::uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, std::uint32_t) override {}
};

/** Returns the long-lived bgfx callback sink used by the engine's render backend. */
BgfxLogSink& LogSinkInstance();

} // namespace Concord::RenderDetail

#endif // CONCORD_BGFXLOGSINK_H