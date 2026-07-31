#include "engine/asset/cook/CookedModel.h"

#include "engine/serialization/BinaryReader.h"
#include "engine/serialization/BinaryWriter.h"

#include <string>

namespace Concord::Asset::CookedModel {

namespace {

using Serialization::BinaryReader;
using Serialization::BinaryWriter;

constexpr std::uint32_t kMagic = 0x4c444d43u; // 'CMDL' in file order
constexpr std::uint32_t kVersion = 1;

// Generous per-blob ceiling; the embedded codecs re-validate their own counts.
constexpr std::size_t kMaxBlobBytes = 512ull * 1024ull * 1024ull;

void PutVec3(BinaryWriter& writer, const Vector3& value)
{
    writer.PutF32(value.x);
    writer.PutF32(value.y);
    writer.PutF32(value.z);
}

Vector3 GetVec3(BinaryReader& reader)
{
    Vector3 value;
    value.x = reader.GetF32();
    value.y = reader.GetF32();
    value.z = reader.GetF32();
    return value;
}

} // namespace

bool LooksLikeCookedModel(const std::uint8_t* data, std::size_t size) noexcept
{
    if (data == nullptr || size < 8) {
        return false;
    }
    const std::uint32_t magic = static_cast<std::uint32_t>(data[0])
        | (static_cast<std::uint32_t>(data[1]) << 8)
        | (static_cast<std::uint32_t>(data[2]) << 16)
        | (static_cast<std::uint32_t>(data[3]) << 24);
    return magic == kMagic;
}

std::vector<std::uint8_t> Encode(const ImportedModel& model)
{
    if (model.IsSkinned() || !model.HasGeometry()) {
        return {};
    }

    BinaryWriter writer;
    writer.PutU32(kMagic);
    writer.PutU32(kVersion);
    writer.PutString(model.name);
    PutVec3(writer, model.boundsMin);
    PutVec3(writer, model.boundsMax);
    writer.PutU32(static_cast<std::uint32_t>(model.meshes.size()));

    for (const ImportedSubMesh& subMesh : model.meshes) {
        const std::vector<std::uint8_t> mesh =
            CookedMesh::Encode(subMesh.geometry);
        const std::vector<std::uint8_t> material =
            CookedMaterial::Encode(subMesh.material);
        writer.PutBytes(mesh.data(), mesh.size());
        writer.PutBytes(material.data(), material.size());
    }
    return writer.Take();
}

std::optional<ImportedModel> Decode(const std::uint8_t* data, std::size_t size,
                                    const CookedModelLimits& limits)
{
    BinaryReader reader(data, size);
    if (reader.GetU32() != kMagic || reader.GetU32() != kVersion) {
        return std::nullopt;
    }

    ImportedModel model;
    model.name = reader.GetString(limits.maxNameBytes);
    model.boundsMin = GetVec3(reader);
    model.boundsMax = GetVec3(reader);

    const std::uint32_t subMeshCount = reader.GetU32();
    if (!reader.Ok() || subMeshCount == 0 || subMeshCount > limits.maxSubMeshes) {
        return std::nullopt;
    }

    model.meshes.reserve(subMeshCount);
    for (std::uint32_t i = 0; i < subMeshCount; ++i) {
        std::string meshBlob;
        reader.GetBytes(meshBlob, kMaxBlobBytes);
        std::string materialBlob;
        reader.GetBytes(materialBlob, kMaxBlobBytes);
        if (!reader.Ok()) {
            return std::nullopt;
        }

        auto mesh = CookedMesh::Decode(
            reinterpret_cast<const std::uint8_t*>(meshBlob.data()),
            meshBlob.size(), limits.mesh);
        auto material = CookedMaterial::Decode(
            reinterpret_cast<const std::uint8_t*>(materialBlob.data()),
            materialBlob.size(), limits.material);
        if (!mesh || !material) {
            return std::nullopt;
        }

        ImportedSubMesh subMesh;
        subMesh.geometry = std::move(*mesh);
        subMesh.material = std::move(*material);
        model.meshes.push_back(std::move(subMesh));
    }

    if (!reader.AtEnd()) {
        return std::nullopt;
    }
    return model;
}

} // namespace Concord::Asset::CookedModel
