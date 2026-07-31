#include "engine/asset/cook/TextureCookProducer.h"

#include "engine/asset/cook/CookedTexture.h"
#include "engine/asset/id/AssetType.h"

#include <string_view>
#include <utility>

namespace Concord::Asset {

namespace {

/**
 * Color textures cook as sRGB; normal/mask/data maps must average in storage
 * space. The opt-out is by naming convention on the identity key, the only
 * per-asset authoring channel the catalog carries today.
 */
bool WantsSrgb(std::string_view key) noexcept
{
    return key.find("_linear") == std::string_view::npos
        && key.find("_data") == std::string_view::npos
        && key.find("_normal") == std::string_view::npos;
}

} // namespace

CookProduceFn MakeTextureCookProduce(TextureDecodeFn decode, CookProduceFn fallback)
{
    return [decode = std::move(decode), fallback = std::move(fallback)](
               const CookCatalogEntry& entry, AssetContentHash resolvedHash,
               std::string& errorOut) -> std::optional<std::vector<std::uint8_t>> {
        if (entry.id.Type() != AssetType::Texture || !decode) {
            return fallback(entry, resolvedHash, errorOut);
        }
        if (entry.sourceBytes.empty()) {
            errorOut = "texture entry has no source bytes: " + entry.id.Key();
            return std::nullopt;
        }

        RgbaImage image;
        const std::span<const std::uint8_t> bytes(
            reinterpret_cast<const std::uint8_t*>(entry.sourceBytes.data()),
            entry.sourceBytes.size());
        if (!decode(bytes, image, errorOut)) {
            if (errorOut.empty()) {
                errorOut = "texture decode failed: " + entry.id.Key();
            }
            return std::nullopt;
        }
        if (!image.IsValid()) {
            errorOut = "texture decode produced an invalid image: " + entry.id.Key();
            return std::nullopt;
        }

        const CookedTextureData texture =
            BuildRgba8MipChain(image, WantsSrgb(entry.id.Key()));
        if (texture.mips.empty()) {
            errorOut = "mip chain generation failed: " + entry.id.Key();
            return std::nullopt;
        }
        std::vector<std::uint8_t> cooked = CookedTexture::Encode(texture);
        if (cooked.empty()) {
            errorOut = "cooked texture encoding failed: " + entry.id.Key();
            return std::nullopt;
        }
        return cooked;
    };
}

} // namespace Concord::Asset
