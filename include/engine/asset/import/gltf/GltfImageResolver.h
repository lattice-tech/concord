#ifndef CONCORD_GLTF_IMAGERESOLVER_H
#define CONCORD_GLTF_IMAGERESOLVER_H

#include "engine/asset/import/gltf/GltfJson.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Concord::Asset::Gltf {

/**
 * Resolves a glTF texture (texture -> source image + sampler) to the image path
 * Concord's material will intern. glTF textures also carry a TEXCOORD set
 * index; the engine only supports set 0, so non-zero sets are used anyway (the
 * importer picks UV0 regardless). Embedded images are written to a cache file
 * the texture loader reads back; returns an empty string on failure.
 */
std::string ResolveTexturePath(const JsonValue& texture,
                               const std::vector<JsonValue>& images,
                               const std::vector<std::vector<std::uint8_t>>& buffers,
                               const std::vector<JsonValue>& bufferViews,
                               const std::string& dir);

} // namespace Concord::Asset::Gltf

#endif // CONCORD_GLTF_IMAGERESOLVER_H
