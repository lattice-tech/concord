#ifndef CONCORD_MODELDESC_H
#define CONCORD_MODELDESC_H

#include "engine/material/MaterialDesc.h"
#include "engine/object/Transform.h"

#include <string>

namespace Concord::Object {

/**
 * Every field a Model node can be constructed from.
 *
 * A plain aggregate so a caller builds one with designated initializers, naming
 * only the fields it cares about:
 *   `ModelDesc{.path = "Assets/models/tree.obj", .transform = {...}}`.
 *
 * Placement pipeline (once, at load — never at draw):
 *   1. Format importer bakes author node/mesh matrices into vertex positions.
 *   2. Optional winding flip.
 *   3. Optional `autoNormalize` rewrites vertices into stable model space
 *      (XZ-centred footprint, uniform fit, ground at y = 0).
 *   4. Draw time multiplies only the node world matrix (Transform) — so the
 *      mesh cannot "swim" with the camera.
 */
struct ModelDesc {
    /** Where the node starts in the scene (parent-relative). */
    Transform transform{};

    /**
     * Path to the model file (OBJ, glTF/GLB, STL, PLY, 3DS, DAE, ...). The
     * extension selects the importer; relative paths resolve against the
     * working directory (typically the folder next to the executable).
     */
    std::string path;

    /** When true, `materialOverride` replaces every imported sub-mesh material. */
    bool overrideMaterial = false;

    /** Material applied to every sub-mesh when `overrideMaterial` is true. */
    Material::MaterialDesc materialOverride{};

    /**
     * True (the default) to rewrite vertices into a stable model-local frame:
     * XZ-center the footprint, uniform-scale so the longest axis spans 2 units,
     * ground-align min Y to 0. The node Transform then places that mesh; there
     * is no second normalize matrix at draw time.
     *
     * Set false to keep author units (useful for architectural scenes already
     * authored in engine metres — then use Transform::scale only if needed).
     */
    bool autoNormalize = true;

    /**
     * True to reverse the triangle winding of every sub-mesh. Some formats and
     * exporters wind CCW-inward, which the engine's default back-face culling
     * would hide; flip this when an imported model appears inside-out.
     */
    bool flipWinding = false;
};

} // namespace Concord::Object

#endif // CONCORD_MODELDESC_H
