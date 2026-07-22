#ifndef CONCORD_STLIMPORTER_H
#define CONCORD_STLIMPORTER_H

#include "engine/asset/import/IModelImporter.h"

namespace Concord::Asset {

/**
 * Imports stereolithography files (.stl), the bare-triangle format ubiquitous
 * in 3D-printing and CAD exports.
 *
 * Auto-detects the binary form (80-byte header, triangle count, then 50-byte
 * facet records) versus the ASCII form (`facet normal`/`endfacet` blocks) by
 * sniffing for the ASCII keyword at the file's start, then parsing
 * accordingly. STL carries no UVs and only per-face normals, so the importer
 * de-duplicates identical vertices (same position) and averages shared normals
 * for smooth shading where faces meet. The result is one sub-mesh with a
 * neutral dielectric material; a caller overrides it through ModelDesc.
 */
class StlImporter final : public IModelImporter {
public:
    bool SupportsExtension(std::string_view ext) const override;
    ImportedModel Import(const std::string& path) override;
};

} // namespace Concord::Asset

#endif // CONCORD_STLIMPORTER_H
