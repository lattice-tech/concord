#ifndef CONCORD_THREEDS_MESHPARSER_H
#define CONCORD_THREEDS_MESHPARSER_H

#include "engine/asset/import/threeds/ThreeDsChunkReader.h"
#include "math/Vector2.h"
#include "math/Vector3.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Concord::Asset::ThreeDs {

/**
 * One triangle mesh parsed from a 3DS named object, before transform baking
 * and axis conversion. 3DS stores vertices, faces, texture coordinates and an
 * optional local matrix as separate sub-chunks under one object; this struct
 * gathers them so the importer can finalize each mesh into an ImportedSubMesh.
 */
struct ParsedMesh {
    /** Object name (the null-terminated string after the 0x4000 chunk id). */
    std::string name;

    /** Vertex positions in the object's local space (matrix not yet applied). */
    std::vector<Vector3> positions;

    /** Texture coordinates per vertex; empty when the object has none. */
    std::vector<Vector2> uvs;

    /** Triangle faces: three vertex indices each. */
    struct Face {
        std::uint16_t v[3];
    };
    std::vector<Face> faces;

    /** Per-face material name (empty string when a face has no assignment). */
    std::vector<std::string> faceMaterials;

    /**
     * The object's local-to-world matrix as 12 floats (4 rows x 3 columns,
     * row-major). Identity when the 0x4160 chunk was absent.
     */
    std::array<float, 12> matrix{};
    bool hasMatrix = false;
};

/**
 * Parses every named object's triangle mesh from the EDIT3DS (0x3D3D) chunk.
 *
 * Reads the 0x4000 named-object chunks, descending into each 0x4100 (triangle
 * mesh) to collect vertices (0x4110), faces (0x4120) with their material
 * assignments (0x4130), texture coordinates (0x4140) and the local matrix
 * (0x4160). Objects that are lights or cameras (not meshes) are skipped.
 *
 * @param reader Positioned at the start of the EDIT3DS chunk's data.
 * @param edit The EDIT3DS chunk (bounds the scan).
 * @return One ParsedMesh per triangle-mesh object, in file order.
 */
std::vector<ParsedMesh> ParseMeshes(ChunkReader& reader, const Chunk& edit);

} // namespace Concord::Asset::ThreeDs

#endif // CONCORD_THREEDS_MESHPARSER_H
