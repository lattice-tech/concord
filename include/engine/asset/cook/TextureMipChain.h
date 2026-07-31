#ifndef CONCORD_TEXTUREMIPCHAIN_H
#define CONCORD_TEXTUREMIPCHAIN_H

#include "engine/asset/cook/CookedTexture.h"

#include <cstdint>
#include <vector>

namespace Concord::Asset {

/** A decoded RGBA8 source image (4 bytes per pixel, row-major, no padding). */
struct RgbaImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;

    bool IsValid() const noexcept
    {
        return width > 0 && height > 0
            && pixels.size() == static_cast<std::size_t>(width)
                * static_cast<std::size_t>(height) * 4u;
    }
};

/**
 * @brief Builds the full RGBA8 mip chain for a decoded image, down to 1x1.
 *
 * Each level averages the 2x2 footprint of the previous one (odd edges clamp
 * to the last row/column, so non-power-of-two sizes are supported). When
 * `srgb` is true the color channels are converted to linear before averaging
 * and re-encoded afterwards — averaging gamma-encoded values directly darkens
 * every downsampled level, which is the classic "mips get dim" artifact.
 * Alpha is coverage, not color, and always averages linearly.
 *
 * Returns an empty-mips CookedTextureData when the image is invalid.
 */
CookedTextureData BuildRgba8MipChain(const RgbaImage& image, bool srgb);

} // namespace Concord::Asset

#endif // CONCORD_TEXTUREMIPCHAIN_H
