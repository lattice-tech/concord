#include "engine/asset/cook/CookedMesh.h"

#include "engine/serialization/BinaryReader.h"
#include "engine/serialization/BinaryWriter.h"

namespace Concord::Asset::CookedMesh {

namespace {

using Serialization::BinaryReader;
using Serialization::BinaryWriter;

constexpr std::uint32_t kMagic = 0x4853454du; // 'MESH' in file order
constexpr std::uint32_t kVersion = 1;

// Vertex-stream presence flags packed into one byte after the header.
constexpr std::uint8_t kHasNormals = 1u << 0;
constexpr std::uint8_t kHasUvs = 1u << 1;
constexpr std::uint8_t kHasSkin = 1u << 2;
constexpr std::uint8_t kIndices32 = 1u << 3;
constexpr std::uint8_t kKnownFlags =
    kHasNormals | kHasUvs | kHasSkin | kIndices32;

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

std::uint8_t VertexFlags(const MeshData& mesh)
{
    std::uint8_t flags = 0;
    if (!mesh.normals.empty()) flags |= kHasNormals;
    if (!mesh.uvs.empty()) flags |= kHasUvs;
    if (mesh.HasSkin()) flags |= kHasSkin;
    if (!mesh.indices32.empty()) flags |= kIndices32;
    return flags;
}

} // namespace

std::vector<std::uint8_t> Encode(const MeshData& mesh)
{
    const std::uint8_t flags = VertexFlags(mesh);
    const std::size_t vertexCount = mesh.positions.size();

    BinaryWriter writer;
    writer.PutU32(kMagic);
    writer.PutU32(kVersion);
    writer.PutU8(flags);
    writer.PutU32(static_cast<std::uint32_t>(vertexCount));

    for (const Vector3& position : mesh.positions) {
        PutVec3(writer, position);
    }
    if (flags & kHasNormals) {
        for (const Vector3& normal : mesh.normals) {
            PutVec3(writer, normal);
        }
    }
    if (flags & kHasUvs) {
        for (const Vector2& uv : mesh.uvs) {
            writer.PutF32(uv.x);
            writer.PutF32(uv.y);
        }
    }
    if (flags & kHasSkin) {
        for (const std::array<std::uint16_t, 4>& bone : mesh.boneIndices) {
            for (const std::uint16_t index : bone) {
                writer.PutU16(index);
            }
        }
        for (const std::array<float, 4>& weight : mesh.boneWeights) {
            for (const float value : weight) {
                writer.PutF32(value);
            }
        }
    }

    if (flags & kIndices32) {
        writer.PutU32(static_cast<std::uint32_t>(mesh.indices32.size()));
        for (const std::uint32_t index : mesh.indices32) {
            writer.PutU32(index);
        }
    } else {
        writer.PutU32(static_cast<std::uint32_t>(mesh.indices.size()));
        for (const std::uint16_t index : mesh.indices) {
            writer.PutU16(index);
        }
    }
    return writer.Take();
}

std::optional<MeshData> Decode(const std::uint8_t* data, std::size_t size,
                               const CookedMeshLimits& limits)
{
    BinaryReader reader(data, size);
    if (reader.GetU32() != kMagic || reader.GetU32() != kVersion) {
        return std::nullopt;
    }
    const std::uint8_t flags = reader.GetU8();
    const std::uint32_t vertexCount = reader.GetU32();
    if (!reader.Ok() || (flags & ~kKnownFlags) != 0
        || vertexCount > limits.maxVertices) {
        return std::nullopt;
    }

    MeshData mesh;
    mesh.positions.resize(vertexCount);
    for (Vector3& position : mesh.positions) {
        position = GetVec3(reader);
    }
    if (flags & kHasNormals) {
        mesh.normals.resize(vertexCount);
        for (Vector3& normal : mesh.normals) {
            normal = GetVec3(reader);
        }
    }
    if (flags & kHasUvs) {
        mesh.uvs.resize(vertexCount);
        for (Vector2& uv : mesh.uvs) {
            uv.x = reader.GetF32();
            uv.y = reader.GetF32();
        }
    }
    if (flags & kHasSkin) {
        mesh.boneIndices.resize(vertexCount);
        for (std::array<std::uint16_t, 4>& bone : mesh.boneIndices) {
            for (std::uint16_t& index : bone) {
                index = reader.GetU16();
            }
        }
        mesh.boneWeights.resize(vertexCount);
        for (std::array<float, 4>& weight : mesh.boneWeights) {
            for (float& value : weight) {
                value = reader.GetF32();
            }
        }
    }

    const std::uint32_t indexCount = reader.GetU32();
    if (!reader.Ok() || indexCount > limits.maxIndices || indexCount % 3u != 0u) {
        return std::nullopt;
    }
    if (flags & kIndices32) {
        mesh.indices32.resize(indexCount);
        for (std::uint32_t& index : mesh.indices32) {
            index = reader.GetU32();
            if (index >= vertexCount) {
                return std::nullopt;
            }
        }
    } else {
        mesh.indices.resize(indexCount);
        for (std::uint16_t& index : mesh.indices) {
            index = reader.GetU16();
            if (index >= vertexCount) {
                return std::nullopt;
            }
        }
    }

    // A well-formed blob is consumed exactly; trailing bytes mean corruption.
    if (!reader.AtEnd()) {
        return std::nullopt;
    }
    return mesh;
}

} // namespace Concord::Asset::CookedMesh
