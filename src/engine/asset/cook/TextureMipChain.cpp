#include "engine/asset/cook/TextureMipChain.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Concord::Asset {

namespace {

/** sRGB byte -> linear float, exact piecewise curve (IEC 61966-2-1). */
float SrgbToLinear(std::uint8_t value) noexcept
{
    const float c = static_cast<float>(value) / 255.0f;
    return c <= 0.04045f ? c / 12.92f
                         : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

/** Linear float -> sRGB byte, rounded to nearest. */
std::uint8_t LinearToSrgb(float value) noexcept
{
    value = std::clamp(value, 0.0f, 1.0f);
    const float c = value <= 0.0031308f ? value * 12.92f
                                        : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
    return static_cast<std::uint8_t>(c * 255.0f + 0.5f);
}

/** Samples pixel (x, y) clamped to the level bounds; returns the 4-byte texel. */
const std::uint8_t* TexelAt(const std::vector<std::uint8_t>& level,
                            std::uint32_t width, std::uint32_t height,
                            std::uint32_t x, std::uint32_t y) noexcept
{
    x = std::min(x, width - 1u);
    y = std::min(y, height - 1u);
    return level.data()
        + (static_cast<std::size_t>(y) * width + x) * 4u;
}

/** Downsamples one RGBA8 level to half size with a clamped 2x2 box filter. */
std::vector<std::uint8_t> Downsample(const std::vector<std::uint8_t>& level,
                                     std::uint32_t width, std::uint32_t height,
                                     std::uint32_t nextWidth, std::uint32_t nextHeight,
                                     bool srgb)
{
    std::vector<std::uint8_t> next(static_cast<std::size_t>(nextWidth)
                                   * static_cast<std::size_t>(nextHeight) * 4u);
    for (std::uint32_t y = 0; y < nextHeight; ++y) {
        for (std::uint32_t x = 0; x < nextWidth; ++x) {
            const std::array<const std::uint8_t*, 4> taps = {
                TexelAt(level, width, height, x * 2u,      y * 2u),
                TexelAt(level, width, height, x * 2u + 1u, y * 2u),
                TexelAt(level, width, height, x * 2u,      y * 2u + 1u),
                TexelAt(level, width, height, x * 2u + 1u, y * 2u + 1u),
            };
            std::uint8_t* out = next.data()
                + (static_cast<std::size_t>(y) * nextWidth + x) * 4u;
            for (int channel = 0; channel < 3; ++channel) {
                if (srgb) {
                    float sum = 0.0f;
                    for (const std::uint8_t* tap : taps) {
                        sum += SrgbToLinear(tap[channel]);
                    }
                    out[channel] = LinearToSrgb(sum * 0.25f);
                } else {
                    std::uint32_t sum = 0;
                    for (const std::uint8_t* tap : taps) {
                        sum += tap[channel];
                    }
                    out[channel] = static_cast<std::uint8_t>((sum + 2u) / 4u);
                }
            }
            std::uint32_t alpha = 0;
            for (const std::uint8_t* tap : taps) {
                alpha += tap[3];
            }
            out[3] = static_cast<std::uint8_t>((alpha + 2u) / 4u);
        }
    }
    return next;
}

} // namespace

CookedTextureData BuildRgba8MipChain(const RgbaImage& image, bool srgb)
{
    CookedTextureData texture;
    if (!image.IsValid()) {
        return texture;
    }
    texture.width = image.width;
    texture.height = image.height;
    texture.format = srgb ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8;
    texture.mips.push_back(image.pixels);

    std::uint32_t width = image.width;
    std::uint32_t height = image.height;
    while (width > 1u || height > 1u) {
        const std::uint32_t nextWidth = std::max(width / 2u, 1u);
        const std::uint32_t nextHeight = std::max(height / 2u, 1u);
        texture.mips.push_back(Downsample(texture.mips.back(), width, height,
                                          nextWidth, nextHeight, srgb));
        width = nextWidth;
        height = nextHeight;
    }
    return texture;
}

} // namespace Concord::Asset
