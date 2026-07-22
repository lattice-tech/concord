#ifndef CONCORD_DAE_PRIMITIVEBUILDER_H
#define CONCORD_DAE_PRIMITIVEBUILDER_H

#include "engine/asset/import/dae/DaeMeshBuilder.h"
#include "engine/asset/import/dae/DaeSourceTable.h"
#include "engine/asset/import/dae/XmlNode.h"

namespace Concord::Asset::Dae {

/**
 * Builds one drawable sub-mesh from a single `<triangles>` or `<polylist>`
 * element.
 *
 * Resolves each `<input>` semantic (VERTEX, NORMAL, TEXCOORD) against `sources`
 * (VERTEX is resolved through `mesh`'s `<vertices>` wrapper), collects the
 * per-corner attribute index triples, then de-duplicates corners by that full
 * triple so vertices sharing a position but differing in normal or UV stay
 * distinct (preserving hard edges and UV seams). Missing normals are generated
 * per-face (flat shading); missing UVs default to (0, 0). Meshes exceeding
 * 65535 vertices use 32-bit indices automatically. Returns a sub-mesh with
 * empty geometry when the primitive is malformed or has no usable positions.
 *
 * @param primitive The `<triangles>` or `<polylist>` element to build from.
 * @param mesh The enclosing `<mesh>` element (used to resolve VERTEX inputs).
 * @param sources The source table for the enclosing `<mesh>`.
 * @return The built sub-mesh, or one with empty geometry on failure.
 */
DaeBuiltSubMesh BuildFromPrimitive(const XmlNode& primitive,
                                   const XmlNode& mesh,
                                   const DaeSourceTable& sources);

} // namespace Concord::Asset::Dae

#endif // CONCORD_DAE_PRIMITIVEBUILDER_H
