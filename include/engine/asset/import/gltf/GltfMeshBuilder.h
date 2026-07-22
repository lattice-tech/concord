#ifndef CONCORD_GLTF_MESHBUILDER_H
#define CONCORD_GLTF_MESHBUILDER_H

#include "engine/asset/import/gltf/GltfJson.h"
#include "engine/asset/import/ImportedModel.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Concord::Asset::Gltf {

/**
 * Imports one glTF primitive (one mesh/material pair) into a sub-mesh.
 *
 * Reads POSITION/NORMAL/TEXCOORD_0 attributes and optional indices, baking the
 * supplied node local transform (translation/rotation/scale) into the
 * positions so the sub-mesh is already in its final pose. Non-indexed
 * primitives synthesize a 0..N-1 index list; meshes over 65535 vertices use
 * 32-bit indices. The primitive's material index, when present, is resolved to
 * a Concord material.
 *
 * @param transform Column-major 4x4 world matrix baked into the geometry.
 */
ImportedSubMesh ImportPrimitive(const JsonValue& primitive,
                                const std::vector<JsonValue>& accessors,
                                const std::vector<std::vector<std::uint8_t>>& buffers,
                                const std::vector<JsonValue>& bufferViews,
                                const std::vector<JsonValue>& materials,
                                const std::vector<JsonValue>& textures,
                                const std::vector<JsonValue>& images,
                                const std::string& dir,
                                float transform[16]);

} // namespace Concord::Asset::Gltf

#endif // CONCORD_GLTF_MESHBUILDER_H
