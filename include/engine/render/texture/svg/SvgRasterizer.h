#ifndef CONCORD_SVGRASTERIZER_H
#define CONCORD_SVGRASTERIZER_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Concord::Render::Svg {

/**
 * @brief CPU-side RGBA8 image decoded from a simple SVG asset.
 */
struct RasterImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;
};

/**
 * @brief Parses and rasterizes a compact editor-icon SVG into RGBA8 pixels.
 */
std::optional<RasterImage> Rasterize(const std::string& path,
                                     const std::vector<std::uint8_t>& bytes);

} // namespace Concord::Render::Svg

#endif // CONCORD_SVGRASTERIZER_H
