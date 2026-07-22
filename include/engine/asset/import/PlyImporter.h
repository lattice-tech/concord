#ifndef CONCORD_PLYIMPORTER_H
#define CONCORD_PLYIMPORTER_H

#include "engine/asset/import/IModelImporter.h"

namespace Concord::Asset {

/**
 * Imports Stanford PLY files (.ply), a flexible format common for scanned and
 * point-cloud geometry.
 *
 * Parses the ASCII header to discover the vertex/face element layouts and the
 * data encoding (ASCII or binary little-endian), then reads position, normal
 * and texture-coordinate properties (whichever the file provides) followed by
 * the face index lists. Polygons fan-triangulate to triangles; vertices are
 * referenced by index rather than duplicated. Files without normals get flat
 * per-face normals generated from the triangle winding. The result is one
 * sub-mesh with a neutral material.
 */
class PlyImporter final : public IModelImporter {
public:
    bool SupportsExtension(std::string_view ext) const override;
    ImportedModel Import(const std::string& path) override;
};

} // namespace Concord::Asset

#endif // CONCORD_PLYIMPORTER_H
