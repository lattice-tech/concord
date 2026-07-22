#ifndef CONCORD_GLTF_SCENEWALKER_H
#define CONCORD_GLTF_SCENEWALKER_H

#include "engine/asset/import/gltf/GltfJson.h"
#include "engine/asset/import/ImportedModel.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Concord::Asset::Gltf {

/**
 * Walks the node hierarchy starting at `nodeIndex`, importing every mesh
 * primitive with the composed world transform accumulated from ancestors.
 *
 * glTF nodes form a forest; scene.roots lists the top-level nodes. Each visited
 * mesh primitive is appended to `model` in its final world pose.
 *
 * @param parentMatrix Column-major 4x4 world transform of the parent node.
 */
void WalkNodes(const std::vector<JsonValue>& nodes,
               const std::vector<JsonValue>& meshes,
               const std::vector<JsonValue>& accessors,
               const std::vector<std::vector<std::uint8_t>>& buffers,
               const std::vector<JsonValue>& bufferViews,
               const std::vector<JsonValue>& materials,
               const std::vector<JsonValue>& textures,
               const std::vector<JsonValue>& images,
               const std::string& dir,
               int nodeIndex,
               const float parentMatrix[16],
               ImportedModel& model);

} // namespace Concord::Asset::Gltf

#endif // CONCORD_GLTF_SCENEWALKER_H
