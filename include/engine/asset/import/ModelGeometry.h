#ifndef CONCORD_MODELGEOMETRY_H
#define CONCORD_MODELGEOMETRY_H

#include "engine/asset/import/ImportedModel.h"

namespace Concord::Asset {

/**
 * Options for the post-import geometry finalize pass (see FinalizeModelGeometry).
 *
 * Importers produce author-space vertices with all node/mesh matrices already
 * baked in. Finalize is the single place that then rewrites those vertices into
 * a stable **model space** the runtime can place with only the node world matrix
 * — never a second compound transform at draw time. That separation is what
 * keeps imported geometry fixed in the world when the camera moves.
 */
struct ModelGeometryOptions {
    /**
     * When true, recenter and uniform-scale the whole model so its longest axis
     * spans `fitExtent` units, then ground-align so min Y == 0. XZ centering
     * keeps the floor footprint under the origin; Y is only shifted by
     * ground-align (not pre-centered), so vertical structure is preserved.
     */
    bool normalize = true;

    /**
     * Target longest-axis length after normalize (default 2 → roughly the unit
     * cube [-1,1] on the dominant axis). Ignored when normalize is false.
     */
    float fitExtent = 2.0f;
};

/**
 * Recomputes `boundsMin` / `boundsMax` from every sub-mesh position.
 * Safe on empty models (bounds stay zeroed).
 */
void RecomputeBounds(ImportedModel& model) noexcept;

/**
 * Places the model into a stable model-local frame used by Object::Model:
 *
 *  1. XZ-center on the AABB (origin sits over the footprint centre).
 *  2. Uniform scale so the longest axis equals `fitExtent`.
 *  3. Ground-align so the lowest vertex is on y = 0.
 *  4. Renormalize normals after the scale.
 *  5. Recompute bounds.
 *
 * All rewrites happen on CPU vertex data once. Draw time only multiplies the
 * node world matrix — never re-applies this pass.
 */
void NormalizeToModelSpace(ImportedModel& model, float fitExtent = 2.0f) noexcept;

/**
 * Full post-import finalize: drop empty sub-meshes, optional normalize, bounds.
 * Called by Object::Model after the format importer returns.
 */
void FinalizeModelGeometry(ImportedModel& model, const ModelGeometryOptions& options) noexcept;

} // namespace Concord::Asset

#endif // CONCORD_MODELGEOMETRY_H
