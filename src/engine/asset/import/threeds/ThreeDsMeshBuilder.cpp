#include "engine/asset/import/threeds/ThreeDsMeshBuilder.h"

#include "color/Color.h"
#include "engine/asset/import/ImportPaths.h"
#include "engine/asset/import/threeds/ThreeDsNormals.h"
#include "engine/debug/Logger.h"
#include "engine/material/MaterialDesc.h"
#include "engine/render/material/CullMode.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace Concord::Asset::ThreeDs {

namespace {

Material::MaterialDesc FindMaterial(const std::vector<ParsedMaterial>& materials,
                                    const std::string& name)
{
    for (const ParsedMaterial& m : materials) {
        if (m.name == name) {
            return m.desc;
        }
    }
    Material::MaterialDesc fallback;
    fallback.surface.albedo = COLOR_RGB(180, 180, 190);
    fallback.surface.roughness = 0.55f;
    fallback.draw.cull = CullMode::None;
    return fallback;
}

/**
 * Emits one sub-mesh for a material group. Every triangle gets three unique
 * vertices; the normal is the crease-smoothed corner normal (see
 * GenerateCornerNormals) so coplanar faces shade uniformly while hard edges
 * stay faceted.
 */
ImportedSubMesh BuildGroup(const ParsedMesh& mesh,
                           const std::vector<Vector3>& positions,
                           const std::vector<std::array<Vector3, 3>>& cornerNormals,
                           const std::vector<std::uint16_t>& faceIndices,
                           Material::MaterialDesc material)
{
    ImportedSubMesh sub;
    material.draw.cull = CullMode::None;
    sub.material = std::move(material);

    const bool hasUvs = !mesh.uvs.empty();
    const bool use32 = faceIndices.size() * 3 > 65535;

    for (std::uint16_t faceIdx : faceIndices) {
        if (faceIdx >= mesh.faces.size()) {
            continue;
        }
        const ParsedMesh::Face& f = mesh.faces[faceIdx];
        if (f.v[0] >= positions.size() || f.v[1] >= positions.size() || f.v[2] >= positions.size()) {
            continue;
        }
        const Vector3& a = positions[f.v[0]];
        const Vector3& b = positions[f.v[1]];
        const Vector3& c = positions[f.v[2]];

        const Vector3* corners[3] = {&a, &b, &c};
        const std::array<Vector3, 3>& n = cornerNormals[faceIdx];
        const std::uint16_t src[3] = {f.v[0], f.v[1], f.v[2]};
        for (int i = 0; i < 3; ++i) {
            const std::uint32_t idx = static_cast<std::uint32_t>(sub.geometry.positions.size());
            sub.geometry.positions.push_back(*corners[i]);
            sub.geometry.normals.push_back(n[i]);
            if (hasUvs && src[i] < mesh.uvs.size()) {
                sub.geometry.uvs.push_back(mesh.uvs[src[i]]);
            } else {
                sub.geometry.uvs.push_back(Vector2{0.0f, 0.0f});
            }
            if (use32) {
                sub.geometry.indices32.push_back(idx);
            } else {
                sub.geometry.indices.push_back(static_cast<std::uint16_t>(idx));
            }
        }
    }
    return sub;
}

} // namespace

std::string ResolveTexturePath(const std::string& modelDirectory, const std::string& mapName)
{
    if (mapName.empty()) {
        return {};
    }

    namespace fs = std::filesystem;
    const std::string joined = Paths::Join(modelDirectory, mapName);
    {
        std::ifstream probe(joined, std::ios::binary);
        if (probe.good()) {
            return joined;
        }
    }

    // Basename only (exporters often embed "C:\\foo\\bar.JPG").
    std::string base = mapName;
    const auto slash = base.find_last_of("/\\");
    if (slash != std::string::npos) {
        base = base.substr(slash + 1);
    }
    if (base.empty()) {
        return {};
    }

    const fs::path dir = modelDirectory.empty() ? fs::path(".") : fs::path(modelDirectory);
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
        return {};
    }

    // Case-insensitive match in the model directory (Windows assets often
    // disagree with the casing written inside the .3ds).
    std::string baseLower = base;
    for (char& c : baseLower) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        std::string name = entry.path().filename().string();
        std::string lower = name;
        for (char& c : lower) {
            if (c >= 'A' && c <= 'Z') {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        if (lower == baseLower) {
            return entry.path().string();
        }
    }
    return {};
}

std::vector<ImportedSubMesh> BuildSubMeshes(
    const ParsedMesh& mesh,
    const std::vector<Vector3>& positions,
    const std::vector<ParsedMaterial>& materials,
    const std::string& modelDirectory)
{
    std::vector<ImportedSubMesh> result;
    if (positions.empty() || mesh.faces.empty()) {
        return result;
    }

    // Crease-smoothed corner normals computed once over the whole mesh (not
    // per material group) so smoothing is not artificially cut at material
    // boundaries on an otherwise flat surface.
    const std::vector<std::array<Vector3, 3>> cornerNormals =
        GenerateCornerNormals(positions, mesh.faces);

    std::vector<std::string> groupOrder;
    std::unordered_map<std::string, std::vector<std::uint16_t>> groups;
    for (std::size_t i = 0; i < mesh.faces.size(); ++i) {
        const std::string& matName =
            (i < mesh.faceMaterials.size()) ? mesh.faceMaterials[i] : std::string{};
        if (groups.find(matName) == groups.end()) {
            groupOrder.push_back(matName);
        }
        groups[matName].push_back(static_cast<std::uint16_t>(i));
    }

    for (const std::string& matName : groupOrder) {
        Material::MaterialDesc matDesc = FindMaterial(materials, matName);
        if (!matDesc.textures.albedo.path.empty()) {
            const std::string resolved =
                ResolveTexturePath(modelDirectory, matDesc.textures.albedo.path);
            if (resolved.empty()) {
                Debug::Logger::Info("Asset",
                                    "3DS: albedo map '%s' not found next to model; using diffuse colour",
                                    matDesc.textures.albedo.path.c_str());
                matDesc.textures.albedo.path.clear();
            } else {
                matDesc.textures.albedo.path = resolved;
            }
        }
        ImportedSubMesh sub =
            BuildGroup(mesh, positions, cornerNormals, groups[matName], std::move(matDesc));
        if (!sub.geometry.positions.empty()) {
            result.push_back(std::move(sub));
        }
    }
    return result;
}

} // namespace Concord::Asset::ThreeDs
