#ifndef CONCORD_DAEIMPORTER_H
#define CONCORD_DAEIMPORTER_H

#include "engine/asset/import/IModelImporter.h"

namespace Concord::Asset {

/**
 * Imports Collada (.dae) files — the ISO-standard XML interchange format,
 * widely available on free-model sites alongside OBJ and glTF.
 *
 * Parses the XML document with a small built-in reader, then resolves the
 * three-layer material indirection (material -> effect -> image), builds
 * indexed geometry from `<triangles>` and `<polylist>` (fan-triangulated, with
 * per-vertex de-duplication across separate normal/UV streams), and walks the
 * node hierarchy baking each `<instance_geometry>`'s composed transform into
 * its vertices so the imported model is already in world-ready pose. Diffuse
 * maps to albedo, shininess to roughness; textures resolve relative to the
 * .dae file's directory.
 */
class DaeImporter final : public IModelImporter {
public:
    bool SupportsExtension(std::string_view ext) const override;
    ImportedModel Import(const std::string& path) override;
};

} // namespace Concord::Asset

#endif // CONCORD_DAEIMPORTER_H
