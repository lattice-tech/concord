#ifndef CONCORD_MATERIALDESC_H
#define CONCORD_MATERIALDESC_H

#include "engine/material/DrawOptions.h"
#include "engine/material/Gradient.h"
#include "engine/material/MaterialModel.h"
#include "engine/material/MaterialTextures.h"
#include "engine/material/Surface.h"

namespace Concord::Material {

/**
 * The complete, caller-facing description of a material.
 *
 * This is the one struct application code hands to an object, e.g.
 * `box.SetMaterial({.model = MaterialModel::Lit,
 *                   .surface = {.albedo = COLOR_ORANGE, .metallic = 1.0f}})`.
 * Every field defaults to a sensible neutral, so a partial designated
 * initializer is always valid and unnamed fields fall back to *this type's*
 * defaults (the same replace-wholesale semantics as BoxDesc / WindowDesc).
 *
 * The pieces are deliberately split across small headers under
 * `engine/material/` (MaterialModel, Surface, Gradient, MaterialTextures) so
 * each concern stays independently readable and reusable; this struct only
 * composes them.
 */
struct MaterialDesc {
    /** Whether and how the scene's lights affect the surface. */
    MaterialModel model = MaterialModel::Lit;

    /** Base color plus the metallic/roughness/emissive response (see Surface). */
    Surface surface{};

    /** Optional two-color gradient painted over the base color. */
    Gradient gradient{};

    /** Optional image maps layered on top of the flat parameters. */
    MaterialTextures textures{};

    /** Depth/culling/ordering behavior — how the surface occludes and is occluded. */
    DrawOptions draw{};

    /**
     * When true, the surface samples a real-time planar reflection map
     * (mirrored-camera offscreen pass) mixed by Fresnel/metallic. Use on
     * mirrors, floors, water. Requires the render backend's planar pass.
     */
    bool planarReflection = false;
};

} // namespace Concord::Material

#endif // CONCORD_MATERIALDESC_H
