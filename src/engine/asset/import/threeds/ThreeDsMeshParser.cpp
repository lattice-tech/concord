#include "engine/asset/import/threeds/ThreeDsMeshParser.h"

#include "engine/debug/Logger.h"

namespace Concord::Asset::ThreeDs {

namespace {

// 3DS chunk ids used by the mesh parser.
constexpr std::uint16_t kNamedObject   = 0x4000;
constexpr std::uint16_t kTriMesh       = 0x4100;
constexpr std::uint16_t kVertices      = 0x4110;
constexpr std::uint16_t kFaces         = 0x4120;
constexpr std::uint16_t kFaceMaterial  = 0x4130;
constexpr std::uint16_t kTexCoords     = 0x4140;
constexpr std::uint16_t kMeshMatrix    = 0x4160;

/**
 * Parses the vertex list sub-chunk (0x4110): a uint16 count followed by
 * count * 3 little-endian floats.
 */
void ParseVertices(ChunkReader& reader, const Chunk& chunk, ParsedMesh& mesh)
{
    const std::uint16_t count = reader.ReadU16();
    mesh.positions.reserve(count);
    for (std::uint16_t i = 0; i < count && reader.HasMore(chunk); ++i) {
        Vector3 v;
        v.x = reader.ReadFloat();
        v.y = reader.ReadFloat();
        v.z = reader.ReadFloat();
        mesh.positions.push_back(v);
    }
}

/**
 * Parses the face list sub-chunk (0x4120): a uint16 face count, then per face
 * three uint16 vertex indices and a uint16 edge-visibility/flags word.
 */
void ParseFaces(ChunkReader& reader, const Chunk& chunk, ParsedMesh& mesh)
{
    const std::uint16_t count = reader.ReadU16();
    mesh.faces.reserve(count);
    mesh.faceMaterials.resize(count);
    for (std::uint16_t i = 0; i < count && reader.HasMore(chunk); ++i) {
        ParsedMesh::Face face;
        face.v[0] = reader.ReadU16();
        face.v[1] = reader.ReadU16();
        face.v[2] = reader.ReadU16();
        (void)reader.ReadU16(); // face flags (edge visibility / smoothing); unused
        mesh.faces.push_back(face);
    }
}

/**
 * Parses the face-material assignment sub-chunk (0x4130): a null-terminated
 * material name, a uint16 count, then that many uint16 face indices. Each
 * named face is tagged in `mesh.faceMaterials` so the importer can split the
 * mesh into sub-meshes by material.
 */
void ParseFaceMaterial(ChunkReader& reader, const Chunk& chunk, ParsedMesh& mesh)
{
    const std::string matName = reader.ReadCString();
    const std::uint16_t count = reader.ReadU16();
    for (std::uint16_t i = 0; i < count && reader.HasMore(chunk); ++i) {
        const std::uint16_t faceIdx = reader.ReadU16();
        if (faceIdx < mesh.faceMaterials.size()) {
            mesh.faceMaterials[faceIdx] = matName;
        }
    }
}

/**
 * Parses the texture-coordinate sub-chunk (0x4140): a uint16 count, then
 * count * 2 floats (u, v). 3DS texture V points downward, so the V is flipped
 * to match the engine's top-left-origin convention used by the other importers.
 */
void ParseTexCoords(ChunkReader& reader, const Chunk& chunk, ParsedMesh& mesh)
{
    const std::uint16_t count = reader.ReadU16();
    mesh.uvs.reserve(count);
    for (std::uint16_t i = 0; i < count && reader.HasMore(chunk); ++i) {
        Vector2 uv;
        uv.x = reader.ReadFloat();
        uv.y = 1.0f - reader.ReadFloat();
        mesh.uvs.push_back(uv);
    }
}

/**
 * Parses the local-matrix sub-chunk (0x4160): 12 floats forming a 4x3 row-major
 * matrix (X/Y/Z basis rows + translation row). Stored verbatim; the importer
 * interprets and bakes it.
 */
void ParseMeshMatrix(ChunkReader& reader, const Chunk& chunk, ParsedMesh& mesh)
{
    for (int i = 0; i < 12 && reader.HasMore(chunk); ++i) {
        mesh.matrix[i] = reader.ReadFloat();
    }
    mesh.hasMatrix = true;
}

/**
 * Descends into a triangle-mesh chunk (0x4100), dispatching each sub-chunk to
 * the matching parser and skipping unrecognized ones.
 */
void ParseTriMesh(ChunkReader& reader, const Chunk& trimesh, ParsedMesh& mesh)
{
    while (reader.HasMore(trimesh)) {
        const Chunk sub = reader.ReadHeader();
        switch (sub.id) {
            case kVertices:     ParseVertices(reader, sub, mesh); break;
            case kFaces:        ParseFaces(reader, sub, mesh);
                                // Face sub-chunks (material assignments) follow
                                // immediately inside the same 0x4120 chunk.
                                while (reader.HasMore(sub)) {
                                    const Chunk fsub = reader.ReadHeader();
                                    if (fsub.id == kFaceMaterial) {
                                        ParseFaceMaterial(reader, fsub, mesh);
                                    }
                                    reader.Skip(fsub);
                                }
                                break;
            case kTexCoords:    ParseTexCoords(reader, sub, mesh); break;
            case kMeshMatrix:   ParseMeshMatrix(reader, sub, mesh); break;
            default:            break;
        }
        reader.Skip(sub);
    }
}

/**
 * Parses one named object (0x4000): reads the object name, then dispatches on
 * the first sub-chunk. Only triangle meshes (0x4100) are collected; lights and
 * cameras are skipped.
 */
bool ParseNamedObject(ChunkReader& reader, const Chunk& obj, ParsedMesh& out)
{
    out.name = reader.ReadCString();
    bool isMesh = false;
    while (reader.HasMore(obj)) {
        const Chunk sub = reader.ReadHeader();
        if (sub.id == kTriMesh) {
            ParseTriMesh(reader, sub, out);
            isMesh = true;
        }
        reader.Skip(sub);
    }
    return isMesh;
}

} // namespace

std::vector<ParsedMesh> ParseMeshes(ChunkReader& reader, const Chunk& edit)
{
    std::vector<ParsedMesh> meshes;
    while (reader.HasMore(edit)) {
        const Chunk obj = reader.ReadHeader();
        if (obj.id == kNamedObject) {
            ParsedMesh mesh;
            if (ParseNamedObject(reader, obj, mesh)) {
                if (!mesh.positions.empty() && !mesh.faces.empty()) {
                    meshes.push_back(std::move(mesh));
                }
            }
        }
        reader.Skip(obj);
    }
    return meshes;
}

} // namespace Concord::Asset::ThreeDs
