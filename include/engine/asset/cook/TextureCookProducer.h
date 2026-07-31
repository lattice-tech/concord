#ifndef CONCORD_TEXTURECOOKPRODUCER_H
#define CONCORD_TEXTURECOOKPRODUCER_H

#include "engine/asset/cook/CookSession.h"
#include "engine/asset/cook/TextureMipChain.h"

#include <functional>
#include <span>
#include <string>

namespace Concord::Asset {

/**
 * Decodes a source image file's bytes (PNG/JPG/TGA/...) into RGBA8. Supplied
 * by the tool layer so this producer stays free of any image library
 * dependency; returns false and fills `errorOut` on failure.
 */
using TextureDecodeFn = std::function<bool(std::span<const std::uint8_t> bytes,
                                           RgbaImage& out, std::string& errorOut)>;

/**
 * @brief Cook producer that bakes AssetType::Texture entries offline.
 *
 * Decodes the source image through `decode`, generates the full mip chain
 * (sRGB-aware for color content — a virtual path containing "_linear" or
 * "_data" opts out, matching the convention for normal/mask/data maps), and
 * encodes the result as a CookedTexture container. A texture source that
 * fails to decode is a hard cook error — shipping an unbaked blob silently
 * would hide broken content. Non-texture entries delegate to `fallback`
 * (typically MakeModelCookProduce), so one producer serves a mixed catalog.
 */
CookProduceFn MakeTextureCookProduce(TextureDecodeFn decode, CookProduceFn fallback);

} // namespace Concord::Asset

#endif // CONCORD_TEXTURECOOKPRODUCER_H
