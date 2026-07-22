#include "engine/asset/import/gltf/GltfSceneWalker.h"

#include "engine/asset/import/gltf/GltfMeshBuilder.h"

#include <cstring>
#include <utility>

namespace Concord::Asset::Gltf {

namespace {

/** Builds a column-major 4x4 (T * R * S) from a glTF node's TRS fields. */
void BuildNodeMatrix(const JsonValue& node, float out[16]) noexcept
{
    // Identity.
    std::memset(out, 0, sizeof(float) * 16);
    out[0] = out[5] = out[10] = out[15] = 1.0f;

    // Scale.
    if (const JsonValue* s = node.Find("scale"); s != nullptr && s->IsArray() && s->array.size() >= 3) {
        out[0]  = static_cast<float>(s->array[0].number);
        out[5]  = static_cast<float>(s->array[1].number);
        out[10] = static_cast<float>(s->array[2].number);
    }
    // Rotation (quaternion xyzw) -> 3x3.
    if (const JsonValue* r = node.Find("rotation"); r != nullptr && r->IsArray() && r->array.size() >= 4) {
        const float x = static_cast<float>(r->array[0].number);
        const float y = static_cast<float>(r->array[1].number);
        const float z = static_cast<float>(r->array[2].number);
        const float w = static_cast<float>(r->array[3].number);
        const float sx = out[0], sy = out[5], sz = out[10];
        // Column-major rotation matrix.
        out[0] = (1 - 2 * (y * y + z * z)) * sx;
        out[1] = (2 * (x * y + w * z)) * sx;
        out[2] = (2 * (x * z - w * y)) * sx;
        out[4] = (2 * (x * y - w * z)) * sy;
        out[5] = (1 - 2 * (x * x + z * z)) * sy;
        out[6] = (2 * (y * z + w * x)) * sy;
        out[8] = (2 * (x * z + w * y)) * sz;
        out[9] = (2 * (y * z - w * x)) * sz;
        out[10] = (1 - 2 * (x * x + y * y)) * sz;
    }
    // Translation.
    if (const JsonValue* t = node.Find("translation"); t != nullptr && t->IsArray() && t->array.size() >= 3) {
        out[12] = static_cast<float>(t->array[0].number);
        out[13] = static_cast<float>(t->array[1].number);
        out[14] = static_cast<float>(t->array[2].number);
    }
}

} // namespace

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
               ImportedModel& model)
{
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes.size())) {
        return;
    }
    const JsonValue& node = nodes[nodeIndex];

    float local[16];
    BuildNodeMatrix(node, local);
    float world[16];
    // world = parentMatrix * local (column-major multiply).
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += parentMatrix[k * 4 + row] * local[col * 4 + k];
            }
            world[col * 4 + row] = sum;
        }
    }

    const int meshIdx = node.IntOr("mesh", -1);
    if (meshIdx >= 0 && meshIdx < static_cast<int>(meshes.size())) {
        const JsonValue& mesh = meshes[meshIdx];
        if (const JsonValue* prims = mesh.Find("primitives"); prims != nullptr && prims->IsArray()) {
            for (const JsonValue& prim : prims->array) {
                ImportedSubMesh sub = ImportPrimitive(
                    prim, accessors, buffers, bufferViews,
                    materials, textures, images, dir, world);
                if (!sub.geometry.positions.empty()) {
                    model.meshes.push_back(std::move(sub));
                }
            }
        }
    }

    if (const JsonValue* children = node.Find("children"); children != nullptr && children->IsArray()) {
        for (const JsonValue& child : children->array) {
            if (child.IsNumber()) {
                WalkNodes(nodes, meshes, accessors, buffers, bufferViews,
                          materials, textures, images, dir,
                          static_cast<int>(child.number), world, model);
            }
        }
    }
}

} // namespace Concord::Asset::Gltf
