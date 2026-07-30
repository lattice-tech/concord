#include "engine/asset/import/ThreeDsImporter.h"

#include "engine/asset/import/ImportPaths.h"
#include "engine/asset/import/ModelGeometry.h"
#include "engine/asset/import/threeds/ThreeDsChunkReader.h"
#include "engine/asset/import/threeds/ThreeDsMaterialParser.h"
#include "engine/asset/import/threeds/ThreeDsMeshBuilder.h"
#include "engine/asset/import/threeds/ThreeDsMeshParser.h"
#include "engine/asset/import/threeds/ThreeDsTransform.h"
#include "engine/debug/Logger.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace Concord::Asset {

namespace {

constexpr std::uint16_t kMain3ds = 0x4D4D;
constexpr std::uint16_t kEdit3ds = 0x3D3D;

const char* ToString(ThreeDs::MatrixPolicy p) noexcept
{
    switch (p) {
        case ThreeDs::MatrixPolicy::Ignore: return "ignore-matrix";
        case ThreeDs::MatrixPolicy::BakeFull: return "bake-full";
        case ThreeDs::MatrixPolicy::BakeRigid: return "bake-rigid";
    }
    return "ignore-matrix";
}

const char* ToString(ThreeDs::AxisPolicy p) noexcept
{
    return p == ThreeDs::AxisPolicy::KeepYUp ? "Y-up keep" : "Z-up convert";
}

} // namespace

bool ThreeDsImporter::SupportsExtension(std::string_view ext) const
{
    return ext == "3ds";
}

ImportedModel ThreeDsImporter::Import(const std::string& path, ImportContext& context)
{
    (void)context;
    ImportedModel model;
    model.name = path;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        Debug::Logger::Error("Asset", "3DS: could not open '%s'", path.c_str());
        return model;
    }
    const std::streamoff fileSize = file.tellg();
    if (fileSize <= 0) {
        return model;
    }
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(fileSize));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);

    ThreeDs::ChunkReader reader(buffer.data(), buffer.size());
    const ThreeDs::Chunk main = reader.ReadHeader();
    if (main.id != kMain3ds) {
        Debug::Logger::Warn("Asset", "3DS: bad magic 0x%04X in '%s'", main.id, path.c_str());
        return model;
    }

    const std::string dir = Paths::Directory(path);
    std::vector<ThreeDs::ParsedMaterial> materials;
    std::vector<ThreeDs::ParsedMesh> meshes;

    while (reader.HasMore(main)) {
        const ThreeDs::Chunk sub = reader.ReadHeader();
        if (sub.id == kEdit3ds) {
            materials = ThreeDs::ParseMaterials(reader, sub);
            reader.Seek(sub.dataStart);
            meshes = ThreeDs::ParseMeshes(reader, sub);
        }
        reader.Skip(sub);
    }

    Debug::Logger::Info("Asset", "3DS: '%s' -> %zu mesh(es), %zu material(s)",
                        path.c_str(), meshes.size(), materials.size());
    if (meshes.empty()) {
        Debug::Logger::Warn("Asset", "3DS: no geometry parsed from '%s'", path.c_str());
        return model;
    }

    // --- Pass 1: raw positions + matrices ---------------------------------
    std::vector<std::vector<Vector3>> sourcePositions;
    std::vector<std::array<float, 12>> matrices;
    std::vector<bool> hasMatrix;
    sourcePositions.reserve(meshes.size());
    matrices.reserve(meshes.size());
    hasMatrix.reserve(meshes.size());
    for (const ThreeDs::ParsedMesh& mesh : meshes) {
        sourcePositions.push_back(mesh.positions);
        matrices.push_back(mesh.matrix);
        hasMatrix.push_back(mesh.hasMatrix);
    }

    // --- Pass 2: pick matrix dialect by scoring whole-file layouts --------
    const ThreeDs::MatrixPolicy matrixPolicy =
        ThreeDs::SelectBestMatrixPolicy(sourcePositions, matrices, hasMatrix);
    std::vector<ThreeDs::PreparedPositions> prepared =
        ThreeDs::ApplyMatrixPolicy(sourcePositions, matrices, hasMatrix, matrixPolicy);

    // --- Pass 3: pick up-axis ---------------------------------------------
    const ThreeDs::AxisPolicy axisPolicy = ThreeDs::SelectBestAxisPolicy(prepared);
    ThreeDs::ApplyAxisPolicy(prepared, axisPolicy);

    Debug::Logger::Info("Asset", "3DS: '%s' transform -> %s, %s",
                        path.c_str(), ToString(matrixPolicy), ToString(axisPolicy));

    // --- Pass 4: build flat-normal sub-meshes in author/scene space -------
    // All placement is baked into positions here. Object::Model then runs
    // FinalizeModelGeometry (optional normalize) and at draw time multiplies
    // only the node world matrix — never these matrices again.
    std::size_t totalTris = 0;
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        std::vector<ImportedSubMesh> subs = ThreeDs::BuildSubMeshes(
            meshes[i], prepared[i].positions, materials, dir);
        for (ImportedSubMesh& sub : subs) {
            totalTris += sub.geometry.indices.size() / 3;
            totalTris += sub.geometry.indices32.size() / 3;
            model.meshes.push_back(std::move(sub));
        }
    }

    RecomputeBounds(model);

    if (!model.HasGeometry()) {
        Debug::Logger::Warn("Asset", "3DS: no drawable sub-meshes in '%s'", path.c_str());
    } else {
        Debug::Logger::Info(
            "Asset",
            "3DS: '%s' ready — %zu sub-mesh(es), %zu tris, "
            "author AABB (%.2f,%.2f,%.2f)..(%.2f,%.2f,%.2f)",
            path.c_str(),
            model.meshes.size(),
            totalTris,
            model.boundsMin.x, model.boundsMin.y, model.boundsMin.z,
            model.boundsMax.x, model.boundsMax.y, model.boundsMax.z);
    }
    return model;
}

} // namespace Concord::Asset
