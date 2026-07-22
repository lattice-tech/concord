#include "engine/asset/import/ObjImporter.h"

#include "engine/asset/import/obj/ObjMaterialLibrary.h"
#include "engine/asset/import/obj/ObjMeshBuilder.h"
#include "engine/asset/import/ImportPaths.h"
#include "engine/debug/Logger.h"
#include "math/Vector2.h"
#include "math/Vector3.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace Concord::Asset {

bool ObjImporter::SupportsExtension(std::string_view ext) const
{
    return ext == "obj";
}

ImportedModel ObjImporter::Import(const std::string& path)
{
    ImportedModel model;
    model.name = path;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        Debug::Logger::Error("Asset", "OBJ: could not open '%s'", path.c_str());
        return model;
    }

    // Source attribute streams (OBJ-indexed, 1-based); groups index into these.
    std::vector<Vector3> srcPositions;
    std::vector<Vector2> srcUvs;
    std::vector<Vector3> srcNormals;
    srcPositions.reserve(8192);
    srcUvs.reserve(8192);
    srcNormals.reserve(8192);

    const std::string dir = Paths::Directory(path);
    Obj::MaterialTable materials;

    std::vector<Obj::Group> groups;
    groups.push_back(Obj::Group{}); // a default group catches faces before any usemtl

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }
        // Fast skip of comments and short lines.
        const char c0 = line[0];
        if (c0 == '#') {
            continue;
        }

        std::istringstream ss(line);
        std::string token;
        if (!(ss >> token)) {
            continue;
        }

        if (token == "v") {
            Vector3 p{};
            ss >> p.x >> p.y >> p.z;
            srcPositions.push_back(p);
        } else if (token == "vt") {
            Vector2 uv{};
            ss >> uv.x >> uv.y;
            srcUvs.push_back(uv);
        } else if (token == "vn") {
            Vector3 n{};
            ss >> n.x >> n.y >> n.z;
            srcNormals.push_back(n);
        } else if (token == "mtllib") {
            std::string mtlName;
            ss >> mtlName;
            const std::string mtlPath = Paths::Join(dir, mtlName);
            auto more = Obj::ParseMtl(mtlPath);
            materials.insert(more.begin(), more.end());
        } else if (token == "usemtl") {
            std::string name;
            ss >> name;
            // Start a fresh group for the new material (only if the current one
            // is non-empty, to avoid stray empty groups).
            if (!groups.back().indices.empty()) {
                groups.push_back(Obj::Group{});
            }
            groups.back().materialName = name;
        } else if (token == "f") {
            Obj::Group& group = groups.back();
            // Parse every vertex index on the face line, then fan-triangulate.
            std::vector<Obj::VertexKey> corners;
            corners.reserve(4);
            std::string vert;
            while (ss >> vert) {
                corners.push_back(Obj::ParseFaceVertex(
                    vert, srcPositions.size(), srcUvs.size(), srcNormals.size()));
            }
            if (corners.size() < 3) {
                continue;
            }
            // Fan triangulation: (0, i, i+1) for i in [1, n-2].
            const std::uint32_t i0 = group.Add(corners[0], srcPositions, srcUvs, srcNormals);
            for (std::size_t i = 1; i + 1 < corners.size(); ++i) {
                const std::uint32_t i1 = group.Add(corners[i], srcPositions, srcUvs, srcNormals);
                const std::uint32_t i2 = group.Add(corners[i + 1], srcPositions, srcUvs, srcNormals);
                group.indices.push_back(i0);
                group.indices.push_back(i1);
                group.indices.push_back(i2);
            }
        }
    }

    // Finalize every non-empty group into a sub-mesh, generating normals where
    // the file omitted them and accumulating whole-model bounds.
    bool boundsInit = false;
    for (Obj::Group& group : groups) {
        if (group.indices.empty()) {
            continue;
        }
        Obj::GenerateFlatNormals(group);
        model.meshes.push_back(Obj::Finalize(group, materials));
        for (const Vector3& p : group.positions) {
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
        Debug::Logger::Warn("Asset", "OBJ: no geometry parsed from '%s'", path.c_str());
    }
    return model;
}

} // namespace Concord::Asset
