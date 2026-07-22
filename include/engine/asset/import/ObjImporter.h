#ifndef CONCORD_OBJIMPORTER_H
#define CONCORD_OBJIMPORTER_H

#include "engine/asset/import/IModelImporter.h"

namespace Concord::Asset {

/**
 * Imports Wavefront OBJ files (.obj), the most widely supported text mesh
 * format and the lingua franca of free downloadable models.
 *
 * Handles the common surface: `v`/`vt`/`vn` vertex streams, `f` faces with any
 * of the four index forms (`v`, `v/vt`, `v//vn`, `v/vt/vn`), polygons with more
 * than three sides (fan-triangulated), and external `.mtl` material libraries
 * referenced by `mtllib`/`usemtl`. Vertices are de-duplicated by their full
 * (position, uv, normal) index triple so identical corners share one entry,
 * keeping the buffer tight. Missing normals are generated per-face (flat
 * shading); missing UVs default to (0, 0). Meshes exceeding 65535 vertices use
 * 32-bit indices automatically.
 *
 * The parser is single-pass and allocation-aware: lines are read with a
 * bounded scanner, attribute streams reserve once, and the de-dup map reuses
 * its buckets across sub-meshes.
 */
class ObjImporter final : public IModelImporter {
public:
    bool SupportsExtension(std::string_view ext) const override;
    ImportedModel Import(const std::string& path) override;
};

} // namespace Concord::Asset

#endif // CONCORD_OBJIMPORTER_H
