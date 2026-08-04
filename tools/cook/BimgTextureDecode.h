#ifndef CONCORD_BIMGTEXTUREDECODE_H
#define CONCORD_BIMGTEXTUREDECODE_H

#include "engine/asset/cook/TextureCookProducer.h"

namespace Concord::Asset {

/**
 * A TextureDecodeFn backed by bimg's image decoders (PNG, JPG, TGA, BMP, HDR,
 * DDS, KTX, ...). Lives in the tool layer so the engine's cook library never
 * links an image library.
 */
TextureDecodeFn MakeBimgTextureDecode();

} // namespace Concord::Asset

#endif // CONCORD_BIMGTEXTUREDECODE_H
