#ifndef CONCORD_GLTFIMPORTER_H
#define CONCORD_GLTFIMPORTER_H

#include "engine/asset/import/IModelImporter.h"

namespace Concord::Asset {

/**
 * Imports glTF 2.0 files (.gltf JSON and .glb binary containers) — the modern,
 * royalty-free standard that Sketchfab and most asset stores ship by default.
 *
 * Decodes the glTF structure with a small purpose-built JSON parser (no
 * third-party dependency): buffers, bufferViews, accessors, meshes/primitives,
 * materials, textures/images/samplers, and the node/scene hierarchy. Per-
 * primitive POSITION/NORMAL/TEXCOORD_0 attributes and indices are read through
 * their accessors with correct component-type and stride math, supporting both
 * interleaved and separate buffer layouts. Material maps
 * (baseColor/metallicRoughness/normal/emissive) map onto Concord's material
 * model; textures resolve from external URIs, base64 data URIs, or GLB-embedded
 * bufferView images (written to a cache file the texture loader reads back).
 * Node local transforms are baked into each sub-mesh so the imported model is
 * already in world-ready pose.
 */
class GltfImporter final : public IModelImporter {
public:
    bool SupportsExtension(std::string_view ext) const override;
    ImportedModel Import(const std::string& path, ImportContext& context) override;
};

} // namespace Concord::Asset

#endif // CONCORD_GLTFIMPORTER_H
