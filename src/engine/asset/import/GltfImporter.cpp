#include "engine/asset/import/GltfImporter.h"

#include "engine/asset/import/gltf/GltfBufferReader.h"
#include "engine/asset/import/gltf/GltfJson.h"
#include "engine/asset/import/gltf/GltfMeshBuilder.h"
#include "engine/asset/import/gltf/GltfSceneWalker.h"
#include "engine/asset/import/gltf/GltfSkinBuilder.h"
#include "engine/asset/import/ImportPaths.h"
#include "engine/debug/Logger.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <string>
#include <vector>

namespace Concord::Asset {

bool GltfImporter::SupportsExtension(std::string_view ext) const
{
    return ext == "gltf" || ext == "glb";
}

ImportedModel GltfImporter::Import(const std::string& path)
{
    using namespace Gltf;

    ImportedModel model;
    model.name = path;

    // Read the whole file; for .gltf it is JSON text, for .glb it is a binary
    // container whose first chunk is the JSON.
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        Debug::Logger::Error("Asset", "glTF: could not open '%s'", path.c_str());
        return model;
    }
    const std::streamsize fileSize = file.tellg();
    if (fileSize <= 0) {
        return model;
    }
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(fileSize));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(raw.data()), fileSize);

    const std::string dir = Paths::Directory(path);
    const std::string ext = Paths::Extension(path);

    std::string jsonText;
    std::vector<std::uint8_t> glbBin; // the GLB BIN chunk (buffer 0 when no uri)

    if (ext == "glb") {
        // GLB: 12-byte header (magic, version, length) then chunks.
        if (raw.size() < 12) {
            Debug::Logger::Warn("Asset", "glTF: GLB too small in '%s'", path.c_str());
            return model;
        }
        const std::uint32_t magic = *reinterpret_cast<const std::uint32_t*>(raw.data());
        if (magic != 0x46546C67) { // "glTF"
            Debug::Logger::Warn("Asset", "glTF: bad GLB magic in '%s'", path.c_str());
            return model;
        }
        std::size_t offset = 12;
        while (offset + 8 <= raw.size()) {
            const std::uint32_t chunkLen = *reinterpret_cast<const std::uint32_t*>(raw.data() + offset);
            const std::uint32_t chunkType = *reinterpret_cast<const std::uint32_t*>(raw.data() + offset + 4);
            offset += 8;
            if (offset + chunkLen > raw.size()) {
                break;
            }
            if (chunkType == 0x4E4F534A) { // "JSON"
                jsonText.assign(reinterpret_cast<const char*>(raw.data() + offset), chunkLen);
            } else if (chunkType == 0x004E4942) { // "BIN\0"
                glbBin.assign(raw.data() + offset, raw.data() + offset + chunkLen);
            }
            offset += chunkLen;
        }
        if (jsonText.empty()) {
            Debug::Logger::Warn("Asset", "glTF: no JSON chunk in GLB '%s'", path.c_str());
            return model;
        }
    } else {
        jsonText.assign(reinterpret_cast<const char*>(raw.data()), raw.size());
    }

    JsonValue root;
    try {
        root = JsonParser(jsonText).Parse();
    } catch (const std::exception& e) {
        Debug::Logger::Warn("Asset", "glTF: JSON parse failed in '%s': %s", path.c_str(), e.what());
        return model;
    }
    if (!root.IsObject()) {
        return model;
    }

    // Gather the data slices from the JSON.
    std::vector<JsonValue> buffers;
    if (const JsonValue* arr = root.Find("buffers"); arr != nullptr && arr->IsArray()) {
        buffers = arr->array;
    }
    std::vector<JsonValue> bufferViews;
    if (const JsonValue* arr = root.Find("bufferViews"); arr != nullptr && arr->IsArray()) {
        bufferViews = arr->array;
    }
    std::vector<JsonValue> accessors;
    if (const JsonValue* arr = root.Find("accessors"); arr != nullptr && arr->IsArray()) {
        accessors = arr->array;
    }
    std::vector<JsonValue> meshes;
    if (const JsonValue* arr = root.Find("meshes"); arr != nullptr && arr->IsArray()) {
        meshes = arr->array;
    }
    std::vector<JsonValue> materials;
    if (const JsonValue* arr = root.Find("materials"); arr != nullptr && arr->IsArray()) {
        materials = arr->array;
    }
    std::vector<JsonValue> textures;
    if (const JsonValue* arr = root.Find("textures"); arr != nullptr && arr->IsArray()) {
        textures = arr->array;
    }
    std::vector<JsonValue> images;
    if (const JsonValue* arr = root.Find("images"); arr != nullptr && arr->IsArray()) {
        images = arr->array;
    }
    std::vector<JsonValue> nodes;
    if (const JsonValue* arr = root.Find("nodes"); arr != nullptr && arr->IsArray()) {
        nodes = arr->array;
    }
    std::vector<JsonValue> skins;
    if (const JsonValue* arr = root.Find("skins"); arr != nullptr && arr->IsArray()) {
        skins = arr->array;
    }
    std::vector<JsonValue> animations;
    if (const JsonValue* arr = root.Find("animations"); arr != nullptr && arr->IsArray()) {
        animations = arr->array;
    }

    // Decode the actual buffer bytes (GLB chunk, data URIs, or external files).
    std::vector<std::vector<std::uint8_t>> bufferBytes;
    bufferBytes.reserve(buffers.size());
    for (const JsonValue& b : buffers) {
        bufferBytes.push_back(LoadBuffer(b, dir, glbBin));
    }

    // Walk the default scene's nodes (or, lacking scenes, every node).
    float identity[16];
    std::memset(identity, 0, sizeof(identity));
    identity[0] = identity[5] = identity[10] = identity[15] = 1.0f;

    // Skinned path: when the file has a skin, build the skeleton and import the
    // skinned meshes in mesh-local space (no node-transform bake — the joint
    // matrices place the vertices), plus every animation as a SkeletalClip.
    bool skinnedPath = false;
    if (!skins.empty() && !nodes.empty()) {
        int skinIdx = -1;
        for (const JsonValue& n : nodes) {
            const int s = n.IntOr("skin", -1);
            if (s >= 0 && s < static_cast<int>(skins.size())) {
                skinIdx = s;
                break;
            }
        }
        if (skinIdx >= 0) {
            std::vector<int> nodeToBone;
            model.skeleton = Gltf::BuildSkeleton(skins[skinIdx], nodes, accessors,
                                                 bufferBytes, bufferViews, nodeToBone);
            if (!model.skeleton.Empty()) {
                skinnedPath = true;
                for (const JsonValue& n : nodes) {
                    if (n.IntOr("skin", -1) < 0) {
                        continue;
                    }
                    const int meshIdx = n.IntOr("mesh", -1);
                    if (meshIdx < 0 || meshIdx >= static_cast<int>(meshes.size())) {
                        continue;
                    }
                    const JsonValue& mesh = meshes[meshIdx];
                    const JsonValue* prims = mesh.Find("primitives");
                    if (prims == nullptr || !prims->IsArray()) {
                        continue;
                    }
                    for (const JsonValue& prim : prims->array) {
                        float xf[16];
                        std::memcpy(xf, identity, sizeof(xf));
                        ImportedSubMesh sub = Gltf::ImportPrimitive(
                            prim, accessors, bufferBytes, bufferViews,
                            materials, textures, images, dir, xf);
                        if (sub.geometry.positions.empty()) {
                            continue;
                        }
                        Gltf::ReadSkinAttributes(prim, accessors, bufferBytes, bufferViews, sub.geometry);
                        model.meshes.push_back(std::move(sub));
                    }
                }
                for (const JsonValue& anim : animations) {
                    Animation::SkeletalClip clip = Gltf::BuildSkeletalClip(
                        anim, accessors, bufferBytes, bufferViews, nodeToBone);
                    if (!clip.tracks.empty()) {
                        model.clips.push_back(std::move(clip));
                    }
                }
                Debug::Logger::Info("Asset",
                                    "glTF: skinned model — %zu bones, %zu sub-mesh(es), %zu clip(s)",
                                    model.skeleton.Count(), model.meshes.size(), model.clips.size());
            }
        }
    }

    const JsonValue* scenes = skinnedPath ? nullptr : root.Find("scenes");
    const int sceneIdx = root.IntOr("scene", 0);
    if (scenes != nullptr && scenes->IsArray() && sceneIdx >= 0 &&
        sceneIdx < static_cast<int>(scenes->array.size())) {
        const JsonValue& scene = scenes->array[sceneIdx];
        if (const JsonValue* roots = scene.Find("nodes"); roots != nullptr && roots->IsArray()) {
            for (const JsonValue& n : roots->array) {
                if (n.IsNumber()) {
                    WalkNodes(nodes, meshes, accessors, bufferBytes, bufferViews,
                              materials, textures, images, dir,
                              n.IntegerOr(-1), identity, model);
                }
            }
        }
    } else if (!skinnedPath && !nodes.empty()) {
        // No scene declared: walk every top-level node.
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
            WalkNodes(nodes, meshes, accessors, bufferBytes, bufferViews,
                      materials, textures, images, dir, i, identity, model);
        }
    }

    // Compute whole-model bounds from every sub-mesh's positions.
    bool boundsInit = false;
    for (const ImportedSubMesh& sub : model.meshes) {
        for (const Vector3& p : sub.geometry.positions) {
            if (!boundsInit) {
                model.boundsMin = model.boundsMax = p;
                boundsInit = true;
            } else {
                model.boundsMin.x = std::min(model.boundsMin.x, p.x);
                model.boundsMin.y = std::min(model.boundsMin.y, p.y);
                model.boundsMin.z = std::min(model.boundsMin.z, p.z);
                model.boundsMax.x = std::max(model.boundsMax.x, p.x);
                model.boundsMax.y = std::max(model.boundsMax.y, p.y);
                model.boundsMax.z = std::max(model.boundsMax.z, p.z);
            }
        }
    }

    if (!model.HasGeometry()) {
        Debug::Logger::Warn("Asset", "glTF: no geometry parsed from '%s'", path.c_str());
    }
    return model;
}

} // namespace Concord::Asset
