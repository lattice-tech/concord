#ifndef CONCORD_THREEDSIMPORTER_H
#define CONCORD_THREEDSIMPORTER_H

#include "engine/asset/import/IModelImporter.h"

namespace Concord::Asset {

/**
 * Imports 3D Studio (.3ds) files — the classic Autodesk binary format, still
 * common on free-model sites as a lightweight interchange option.
 *
 * Parses the chunk-based binary structure with a purpose-built reader: meshes
 * (vertices, faces, texture coordinates, per-face material assignments), the
 * per-object local matrix, and the material table (diffuse color, shininess,
 * texture filename). Each object's local matrix is baked into its vertices,
 * and the 3DS Z-up coordinate system is converted to the engine's Y-up via a
 * -90° X-axis rotation. Faces sharing the same material become one sub-mesh,
 * preserving the artist's material split; flat normals are generated since 3DS
 * stores no vertex normals.
 */
class ThreeDsImporter final : public IModelImporter {
public:
    bool SupportsExtension(std::string_view ext) const override;
    ImportedModel Import(const std::string& path) override;
};

} // namespace Concord::Asset

#endif // CONCORD_THREEDSIMPORTER_H
