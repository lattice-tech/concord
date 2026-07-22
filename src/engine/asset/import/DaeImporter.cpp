#include "engine/asset/import/DaeImporter.h"

#include "engine/asset/import/ImportPaths.h"
#include "engine/asset/import/dae/DaeMaterialResolver.h"
#include "engine/asset/import/dae/DaeMeshBuilder.h"
#include "engine/asset/import/dae/DaeNodeWalker.h"
#include "engine/asset/import/dae/XmlReader.h"
#include "color/Color.h"
#include "engine/debug/Logger.h"
#include "engine/render/mesh/MeshData.h"
#include "math/Vector2.h"
#include "math/Vector3.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace Concord::Asset {

namespace {

/** Finds a <geometry> by id (matching the url fragment of an <instance_geometry>). */
const Dae::XmlNode* FindGeometryById(const Dae::XmlNode& root, std::string_view url)
{
    std::string_view id = url;
    if (!id.empty() && id[0] == '#') {
        id.remove_prefix(1);
    }
    for (const Dae::XmlNode* lib : root.FindChildren("library_geometries")) {
        for (const Dae::XmlNode* geom : lib->FindChildren("geometry")) {
            if (geom->Attr("id") == id) {
                return geom;
            }
        }
    }
    return nullptr;
}

/**
 * Resolves a geometry's material symbol to an actual material id using the
 * node's <bind_material> bindings. When no binding covers the symbol, the
 * symbol itself is treated as the material id (some files omit bind_material).
 */
std::string ResolveMaterialId(const std::vector<std::pair<std::string, std::string>>& bindings,
                              std::string_view symbol)
{
    for (const auto& [sym, target] : bindings) {
        if (sym == symbol) {
            return target;
        }
    }
    return std::string(symbol);
}

/**
 * Bakes a column-major 4x4 transform into every position and rotates every
 * normal by its upper-left 3x3, so the sub-mesh lands in the pose its node
 * chain described. Mirrors the glTF importer's per-primitive transform baking.
 */
void BakeTransform(MeshData& data, const float transform[16])
{
    for (Vector3& p : data.positions) {
        const float x = p.x, y = p.y, z = p.z;
        p = Vector3{
            transform[0] * x + transform[4] * y + transform[8] * z + transform[12],
            transform[1] * x + transform[5] * y + transform[9] * z + transform[13],
            transform[2] * x + transform[6] * y + transform[10] * z + transform[14]};
    }
    for (Vector3& n : data.normals) {
        const float x = n.x, y = n.y, z = n.z;
        n = Vector3{
            transform[0] * x + transform[4] * y + transform[8] * z,
            transform[1] * x + transform[5] * y + transform[9] * z,
            transform[2] * x + transform[6] * y + transform[10] * z};
        // Renormalize: non-uniform scale in the transform can shrink/stretch.
        const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len > 0.0f) {
            n.x /= len; n.y /= len; n.z /= len;
        } else {
            n = Vector3{0.0f, 1.0f, 0.0f};
        }
    }
}

} // namespace

bool DaeImporter::SupportsExtension(std::string_view ext) const
{
    return ext == "dae";
}

ImportedModel DaeImporter::Import(const std::string& path)
{
    ImportedModel model;
    model.name = path;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        Debug::Logger::Error("Asset", "Collada: could not open '%s'", path.c_str());
        return model;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    const std::string text = ss.str();

    const Dae::XmlNode root = Dae::ParseXml(text);
    if (root.name.empty()) {
        Debug::Logger::Warn("Asset", "Collada: no root element parsed from '%s'", path.c_str());
        return model;
    }
    if (root.name != "COLLADA") {
        Debug::Logger::Warn("Asset", "Collada: root element is '%s', expected COLLADA",
                            root.name.c_str());
    }

    const std::string dir = Paths::Directory(path);

    // Load the material table once; every sub-mesh resolves through it.
    Dae::DaeMaterialResolver materials;
    materials.Load(root, dir);

    // Walk the scene's nodes, collecting each <instance_geometry> with its
    // composed world transform and material bindings.
    const std::vector<Dae::DaeNodeInstance> instances = Dae::CollectInstances(root);

    // Cache built geometry by url so the same mesh referenced from several
    // nodes is parsed once, then re-baked per instance.
    std::unordered_map<std::string, std::vector<Dae::DaeBuiltSubMesh>> geomCache;

    bool boundsInit = false;
    for (const Dae::DaeNodeInstance& inst : instances) {
        auto cacheIt = geomCache.find(inst.geometryUrl);
        if (cacheIt == geomCache.end()) {
            const Dae::XmlNode* geom = FindGeometryById(root, inst.geometryUrl);
            if (geom == nullptr) {
                Debug::Logger::Warn("Asset", "Collada: geometry '%s' not found",
                                    inst.geometryUrl.c_str());
                continue;
            }
            cacheIt = geomCache.emplace(inst.geometryUrl, Dae::BuildSubMeshes(*geom)).first;
        }

        for (const Dae::DaeBuiltSubMesh& built : cacheIt->second) {
            ImportedSubMesh sub;
            sub.geometry = built.geometry;

            // Bake this instance's node-chain transform into the geometry.
            BakeTransform(sub.geometry, inst.transform.data());

            // Resolve the material symbol to an id, then to a descriptor.
            const std::string matId = ResolveMaterialId(inst.materialBindings, built.materialSymbol);
            sub.material = materials.Resolve(matId);
            // Ensure the surface is visible even when the file declared no
            // usable material: opaque, double-sided, moderate roughness.
            sub.material.draw.cull = CullMode::None;
            if (sub.material.surface.albedo == COLOR_WHITE &&
                sub.material.surface.metallic == 0.0f &&
                sub.material.surface.roughness == 0.5f) {
                sub.material.surface.albedo = COLOR_RGB(180, 180, 190);
                sub.material.surface.roughness = 0.6f;
            }

            // Accumulate whole-model bounds from the baked positions.
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

            model.meshes.push_back(std::move(sub));
        }
    }

    if (!model.HasGeometry()) {
        Debug::Logger::Warn("Asset", "Collada: no geometry parsed from '%s'", path.c_str());
    } else {
        Debug::Logger::Info("Asset", "Collada: imported '%s': %zu sub-mesh(es)",
                            path.c_str(), model.meshes.size());
    }
    return model;
}

} // namespace Concord::Asset
