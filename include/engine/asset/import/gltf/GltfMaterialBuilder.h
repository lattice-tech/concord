#ifndef CONCORD_GLTF_MATERIALBUILDER_H
#define CONCORD_GLTF_MATERIALBUILDER_H

#include "engine/asset/import/gltf/GltfJson.h"
#include "engine/material/MaterialDesc.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Concord::Asset::Gltf {

/**
 * Builds a Concord material from a glTF material definition.
 *
 * Maps pbrMetallicRoughness (base color, metallic/roughness, their textures),
 * the normal and emissive maps, and the emissive factor onto Concord's material
 * model, resolving each referenced texture to an image path. A doubleSided
 * material disables back-face culling.
 */
Material::MaterialDesc BuildMaterial(const JsonValue& mat,
                                     const std::vector<JsonValue>& textures,
                                     const std::vector<JsonValue>& images,
                                     const std::vector<std::vector<std::uint8_t>>& buffers,
                                     const std::vector<JsonValue>& bufferViews,
                                     const std::string& dir);

} // namespace Concord::Asset::Gltf

#endif // CONCORD_GLTF_MATERIALBUILDER_H
