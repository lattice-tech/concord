#ifndef CONCORD_DAE_MESHBUILDER_H
#define CONCORD_DAE_MESHBUILDER_H

#include "engine/asset/import/dae/XmlNode.h"
#include "engine/render/mesh/MeshData.h"

#include <string>
#include <vector>

namespace Concord::Asset::Dae {

/**
 * One piece of geometry extracted from a Collada `<mesh>, plus the material
 * symbol its `<triangles>/<polylist>` declared. The symbol is later bound to an
 * actual material by the node's `<bind_material>` (see DaeMaterialResolver).
 */
struct DaeBuiltSubMesh {
    /** CPU-side geometry in the mesh's local space (no node transform baked in). */
    MeshData geometry{};

    /** The `material="..."` symbol from the `<triangles>/<polylist>` element. */
    std::string materialSymbol;
};

/**
 * Builds drawable sub-meshes from a Collada `<geometry>`'s `<mesh>` element.
 *
 * Handles `<triangles>` (uniform triangle lists) and `<polylist>` (variable-
 * size polygons, fan-triangulated). Each `<input>` semantic (VERTEX, NORMAL,
 * TEXCOORD) is resolved through its source and de-duplicated by the full
 * attribute index tuple, so vertices sharing a position but differing in
 * normal or UV stay distinct (preserving hard edges and UV seams). Missing
 * normals are generated per-face (flat shading); missing UVs default to (0, 0).
 * Meshes exceeding 65535 vertices use 32-bit indices automatically.
 *
 * @param geometry The `<geometry>` XmlNode (must contain a `<mesh>` child).
 * @return One DaeBuiltSubMesh per `<triangles>/<polylist>`, in document order.
 */
std::vector<DaeBuiltSubMesh> BuildSubMeshes(const XmlNode& geometry);

} // namespace Concord::Asset::Dae

#endif // CONCORD_DAE_MESHBUILDER_H
