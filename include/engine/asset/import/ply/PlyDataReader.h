#ifndef CONCORD_PLY_DATAREADER_H
#define CONCORD_PLY_DATAREADER_H

#include "engine/asset/import/ply/PlyHeader.h"
#include "math/Vector2.h"
#include "math/Vector3.h"

#include <cstdint>
#include <istream>
#include <vector>

namespace Concord::Asset::Ply {

/**
 * Column indices of a vertex element's properties, resolved from the common
 * name aliases (x/y/z, nx/ny/nz, s/t...). A value of -1 means the property is
 * absent, so normals/uvs are only present when all their components resolved.
 */
struct VertexLayout {
    int px = -1, py = -1, pz = -1;
    int nx = -1, ny = -1, nz = -1;
    int tu = -1, tv = -1;

    bool HasPosition() const noexcept { return px >= 0 && py >= 0 && pz >= 0; }
    bool HasNormals() const noexcept { return nx >= 0 && ny >= 0 && nz >= 0; }
    bool HasUvs() const noexcept { return tu >= 0 && tv >= 0; }
};

/** Resolves the vertex element's property columns by their common name aliases. */
VertexLayout ResolveVertexLayout(const PlyElement& vertEl);

/**
 * Reads `vertEl.count` vertices from the stream (ASCII or binary per `binary`)
 * into `positions`, and into `normals`/`uvs` when the layout declares them.
 * Returns false when the declaration exceeds the import limits or the data is
 * truncated or malformed.
 */
bool ReadVertices(std::istream& file, bool binary, const PlyElement& vertEl, const VertexLayout& layout,
                  std::vector<Vector3>& positions, std::vector<Vector3>& normals, std::vector<Vector2>& uvs);

/**
 * Reads `faceEl`'s index lists from the stream and fan-triangulates each face
 * into `indices` (three per triangle). Returns false for malformed data,
 * out-of-range vertex indices, or an import-limit violation.
 */
bool ReadFaces(std::istream& file, bool binary, const PlyElement& faceEl,
               std::uint32_t vertexCount, std::vector<std::uint32_t>& indices);

} // namespace Concord::Asset::Ply

#endif // CONCORD_PLY_DATAREADER_H
