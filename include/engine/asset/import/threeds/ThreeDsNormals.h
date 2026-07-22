#ifndef CONCORD_THREEDSNORMALS_H
#define CONCORD_THREEDSNORMALS_H

#include "engine/asset/import/threeds/ThreeDsMeshParser.h"
#include "math/Vector3.h"

#include <array>
#include <vector>

namespace Concord::Asset::ThreeDs {

/**
 * Per-corner, crease-angle-smoothed triangle normals.
 *
 * For each triangle corner, averages the area-weighted facet normals of every
 * face incident to that source vertex whose facet normal lies within
 * @p creaseDegrees of this face's normal. Coplanar faces (a flat wall, a floor)
 * therefore resolve to one shared smooth normal, which removes the faceted
 * "diamond weave" specular pattern flat per-face normals produce on tessellated
 * architectural meshes. Faces meeting across a sharper edge (box/room corners)
 * fall outside the crease window and keep independent normals, so hard edges
 * stay crisp and highlights do not swim as the camera orbits.
 *
 * @param positions     Final engine-space vertex positions.
 * @param faces         Triangle index list into @p positions.
 * @param creaseDegrees Faces differing by more than this angle are not blended.
 * @return One entry per face, holding the three corner normals in face-corner
 *         order. Degenerate or out-of-range faces yield an up-vector
 *         placeholder; callers that skip such faces can ignore those entries.
 */
std::vector<std::array<Vector3, 3>> GenerateCornerNormals(
    const std::vector<Vector3>& positions,
    const std::vector<ParsedMesh::Face>& faces,
    float creaseDegrees = 40.0f);

} // namespace Concord::Asset::ThreeDs

#endif // CONCORD_THREEDSNORMALS_H
