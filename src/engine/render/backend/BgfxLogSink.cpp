#include "engine/render/backend/BgfxLogSink.h"

#include <cstdarg>
#include <cstdio>
#include <string>

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

void BgfxLogSink::screenShot(const char* filePath, std::uint32_t width, std::uint32_t height,
                             std::uint32_t pitch, bgfx::TextureFormat::Enum format,
                             const void* data, std::uint32_t size, bool yflip)
{
    // bgfx always hands back 4-byte BGRA, which is exactly TGA's channel order,
    // so an uncompressed 32-bit TGA needs no conversion and no image library.
    if (filePath == nullptr || data == nullptr || width == 0 || height == 0
        || size < pitch * height) {
        return;
    }
    (void)format;
    std::string path = filePath;
    if (path.size() < 4 || path.compare(path.size() - 4, 4, ".tga") != 0) {
        path += ".tga";
    }
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return;
    }
    const std::uint8_t header[18] = {
        0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        static_cast<std::uint8_t>(width & 0xffu),
        static_cast<std::uint8_t>((width >> 8) & 0xffu),
        static_cast<std::uint8_t>(height & 0xffu),
        static_cast<std::uint8_t>((height >> 8) & 0xffu),
        32,
        // Bit 5 of the descriptor marks a top-to-bottom image; set it when bgfx
        // did not already flip the rows for us.
        static_cast<std::uint8_t>(yflip ? 0x00u : 0x20u),
    };
    std::fwrite(header, 1, sizeof(header), file);
    const auto* rows = static_cast<const std::uint8_t*>(data);
    for (std::uint32_t row = 0; row < height; ++row) {
        std::fwrite(rows + static_cast<std::size_t>(row) * pitch, 1,
                    static_cast<std::size_t>(width) * 4u, file);
    }
    std::fclose(file);
    std::fprintf(stderr, "[bgfx] screenshot written: %s (%ux%u)\n", path.c_str(), width, height);
    std::fflush(stderr);
}

void BgfxLogSink::traceVargs(const char* filePath, std::uint16_t line, const char* format, va_list argList)
{
    std::fprintf(stderr, "[bgfx] %s:%u: ", filePath, line);
    std::vfprintf(stderr, format, argList);
    std::fflush(stderr);
}

} // namespace Concord::RenderDetail