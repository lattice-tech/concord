#include "engine/render/texture/BgfxTextureCache.h"

#include "engine/debug/Logger.h"
#include "engine/render/texture/TextureRegistry.h"
#include "engine/render/texture/svg/SvgRasterizer.h"

#include <bimg/bimg.h>
#include <bimg/decode.h>
#include <bx/allocator.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace Concord {

namespace {

/** Shared allocator bimg decodes into; stateless, so one static is enough. */
bx::DefaultAllocator& DecodeAllocator()
{
    static bx::DefaultAllocator allocator;
    return allocator;
}

/**
 * 2x2 box filter, RGBA8, producing the next mip level. Source coordinates are
 * clamped so non-power-of-two and Nx1 tail levels stay in-bounds (bimg's own
 * downsample assumes even dimensions and would under-fill those levels).
 */
void BoxDownsampleRgba8(const std::uint8_t* src, std::uint32_t sw, std::uint32_t sh,
                        std::uint8_t* dst) noexcept
{
    const std::uint32_t dw = sw > 1 ? sw / 2 : 1;
    const std::uint32_t dh = sh > 1 ? sh / 2 : 1;
    for (std::uint32_t y = 0; y < dh; ++y) {
        const std::uint32_t sy0 = std::min(y * 2, sh - 1);
        const std::uint32_t sy1 = std::min(sy0 + 1, sh - 1);
        for (std::uint32_t x = 0; x < dw; ++x) {
            const std::uint32_t sx0 = std::min(x * 2, sw - 1);
            const std::uint32_t sx1 = std::min(sx0 + 1, sw - 1);
            const std::uint8_t* p00 = src + (sy0 * sw + sx0) * 4;
            const std::uint8_t* p10 = src + (sy0 * sw + sx1) * 4;
            const std::uint8_t* p01 = src + (sy1 * sw + sx0) * 4;
            const std::uint8_t* p11 = src + (sy1 * sw + sx1) * 4;
            std::uint8_t* d = dst + (y * dw + x) * 4;
            for (int c = 0; c < 4; ++c) {
                d[c] = static_cast<std::uint8_t>(
                    (static_cast<std::uint32_t>(p00[c]) + p10[c] + p01[c] + p11[c] + 2) / 4);
            }
        }
    }
}

/**
 * Uploads an uncompressed image as an RGBA8 texture with a full, CPU-generated
 * mip chain. Without mips the anisotropic sampler set in ApplyMaterial degrades
 * to bilinear, so minified and grazing-angle surfaces alias into moiré. Returns
 * an invalid handle if the mip chain cannot be built.
 */
bgfx::TextureHandle UploadMippedRgba8(std::uint32_t width, std::uint32_t height,
                                      const void* srcData,
                                      bimg::TextureFormat::Enum srcFormat)
{
    const std::uint8_t numMips = bimg::imageGetNumMips(bimg::TextureFormat::RGBA8, width, height);
    const auto total = static_cast<std::uint32_t>(bimg::imageGetSize(
        nullptr, static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height), 1,
        false, true, 1, bimg::TextureFormat::RGBA8));
    if (total == 0) {
        return BGFX_INVALID_HANDLE;
    }

    const bgfx::Memory* mem = bgfx::alloc(total);
    bimg::imageDecodeToRgba8(&DecodeAllocator(), mem->data, srcData, width, height,
                             width * 4, srcFormat);

    std::uint8_t* prev = mem->data;
    std::uint32_t offset = width * height * 4;
    std::uint32_t pw = width;
    std::uint32_t ph = height;
    for (std::uint8_t lod = 1; lod < numMips; ++lod) {
        const std::uint32_t nw = pw > 1 ? pw / 2 : 1;
        const std::uint32_t nh = ph > 1 ? ph / 2 : 1;
        BoxDownsampleRgba8(prev, pw, ph, mem->data + offset);
        prev = mem->data + offset;
        offset += nw * nh * 4;
        pw = nw;
        ph = nh;
    }

    return bgfx::createTexture2D(static_cast<std::uint16_t>(width),
                                 static_cast<std::uint16_t>(height), true, 1,
                                 bgfx::TextureFormat::RGBA8,
                                 BGFX_TEXTURE_NONE, mem);
}

/** Reads a whole file into memory; returns false (and leaves `out` empty) on any error. */
bool ReadFile(const std::string& path, std::vector<std::uint8_t>& out)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    const std::streamsize size = file.tellg();
    if (size <= 0) {
        return false;
    }
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    return static_cast<bool>(file.read(reinterpret_cast<char*>(out.data()), size));
}

bool HasSvgExtension(const std::string& path)
{
    if (path.size() < 4) {
        return false;
    }
    const std::string ext = path.substr(path.size() - 4);
    return std::tolower(static_cast<unsigned char>(ext[0])) == '.'
        && std::tolower(static_cast<unsigned char>(ext[1])) == 's'
        && std::tolower(static_cast<unsigned char>(ext[2])) == 'v'
        && std::tolower(static_cast<unsigned char>(ext[3])) == 'g';
}

bgfx::TextureHandle LoadSvg(const std::string& path, const std::vector<std::uint8_t>& bytes)
{
    const std::optional<Render::Svg::RasterImage> image = Render::Svg::Rasterize(path, bytes);
    if (!image || image->width == 0 || image->height == 0 || image->pixels.empty()) {
        Debug::Logger::Error("Render", "texture load failed: cannot rasterize svg '%s'", path.c_str());
        return BGFX_INVALID_HANDLE;
    }
    return UploadMippedRgba8(image->width, image->height, image->pixels.data(),
                             bimg::TextureFormat::RGBA8);
}

/** Decodes and uploads `path`, or returns an invalid handle (logging why). */
bgfx::TextureHandle Load(const std::string& path)
{
    std::vector<std::uint8_t> bytes;
    if (!ReadFile(path, bytes)) {
        Debug::Logger::Error("Render", "texture load failed: cannot read '%s'", path.c_str());
        return BGFX_INVALID_HANDLE;
    }

    if (HasSvgExtension(path)) {
        const bgfx::TextureHandle handle = LoadSvg(path, bytes);
        if (bgfx::isValid(handle)) {
            Debug::Logger::Debug("Render", "loaded svg texture '%s'", path.c_str());
        }
        return handle;
    }

    bimg::ImageContainer* image = bimg::imageParse(
        &DecodeAllocator(), bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    if (image == nullptr) {
        Debug::Logger::Error("Render", "texture load failed: cannot decode '%s'", path.c_str());
        return BGFX_INVALID_HANDLE;
    }

    const std::uint32_t width = image->m_width;
    const std::uint32_t height = image->m_height;
    const auto format = static_cast<bgfx::TextureFormat::Enum>(image->m_format);

    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
    if (bimg::isCompressed(image->m_format) || image->m_numMips > 1) {
        // Compressed (BC/ETC/...) or already-mipped (DDS/KTX): upload as-is.
        // These cannot be box-downsampled here and normally ship a full chain.
        const bgfx::Memory* mem = bgfx::copy(image->m_data, image->m_size);
        handle = bgfx::createTexture2D(
            static_cast<std::uint16_t>(width),
            static_cast<std::uint16_t>(height),
            image->m_numMips > 1,
            image->m_numLayers,
            format,
            BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
            mem);
    } else {
        // Uncompressed single-level (PNG/JPG/TGA/...): build a mip chain so the
        // anisotropic sampler has levels to filter through (kills moiré).
        handle = UploadMippedRgba8(width, height, image->m_data, image->m_format);
    }
    bimg::imageFree(image);

    if (!bgfx::isValid(handle)) {
        Debug::Logger::Error("Render", "texture load failed: cannot upload '%s'", path.c_str());
        return BGFX_INVALID_HANDLE;
    }

    Debug::Logger::Debug("Render", "loaded texture '%s' (%ux%u)", path.c_str(), width, height);
    return handle;
}

} // namespace

bgfx::TextureHandle BgfxTextureCache::Get(TextureId id)
{
    if (id == TextureId::None) {
        return BGFX_INVALID_HANDLE;
    }

    const auto it = m_textures.find(id);
    if (it != m_textures.end()) {
        return it->second; // may be invalid: a remembered failed load
    }

    const bgfx::TextureHandle handle = Load(TextureRegistry::Path(id));
    m_textures.emplace(id, handle);
    return handle;
}

bgfx::TextureHandle BgfxTextureCache::White()
{
    if (bgfx::isValid(m_white)) {
        return m_white;
    }
    const std::uint32_t pixel = 0xffffffffu;
    m_white = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                                    BGFX_TEXTURE_NONE, bgfx::copy(&pixel, sizeof(pixel)));
    return m_white;
}

void BgfxTextureCache::Clear()
{
    for (auto& [id, handle] : m_textures) {
        if (bgfx::isValid(handle)) {
            bgfx::destroy(handle);
        }
    }
    m_textures.clear();
    if (bgfx::isValid(m_white)) {
        bgfx::destroy(m_white);
        m_white = BGFX_INVALID_HANDLE;
    }
}

} // namespace Concord
