#ifndef CONCORD_THREEDSMESHBUILDER_H
#define CONCORD_THREEDSMESHBUILDER_H

#include "engine/asset/import/ImportedModel.h"
#include "engine/asset/import/threeds/ThreeDsMaterialParser.h"
#include "engine/asset/import/threeds/ThreeDsMeshParser.h"
#include "math/Vector3.h"

#include <string>
#include <vector>

namespace Concord::Asset::ThreeDs {

/**
 * Builds engine sub-meshes from one prepared 3DS object.
 *
 * Positions are already in final engine space (matrix + axis resolved). Each
 * triangle is emitted with its own three vertices and a flat face normal so
 * hard edges never share a blended normal — that blend was the main cause of
 * specular "swimming" when the camera orbits architectural models.
 *
 * Texture paths are resolved relative to `modelDirectory`; missing maps are
 * dropped (flat diffuse colour) instead of left as broken paths.
 */
std::vector<ImportedSubMesh> BuildSubMeshes(
    const ParsedMesh& mesh,
    const std::vector<Vector3>& positions,
    const std::vector<ParsedMaterial>& materials,
    const std::string& modelDirectory);

/**
 * Resolves a texture file next to the model. Tries the joined path, then a
 * case-insensitive match in the model directory. Returns empty when nothing
 * readable is found.
 */
std::string ResolveTexturePath(const std::string& modelDirectory, const std::string& mapName);

} // namespace Concord::Asset::ThreeDs

#endif // CONCORD_THREEDSMESHBUILDER_H
