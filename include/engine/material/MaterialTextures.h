#ifndef CONCORD_MATERIALTEXTURES_H
#define CONCORD_MATERIALTEXTURES_H

#include "engine/material/Texture.h"

namespace Concord::Material {

/**
 * The set of image maps a material can layer on top of its flat parameters.
 *
 * Each map is optional (an empty Texture means "not used"), so a material can
 * mix textured and flat channels freely — e.g. a photographed albedo map over
 * a constant roughness. Maps modulate the matching Surface parameter rather
 * than replacing the whole material, so the flat value acts as a tint/scale
 * when a map is present.
 *
 * A plain aggregate so a caller names only the maps it has, e.g.
 * `.textures = {.albedo = {"crate.png"}, .normal = {"crate_n.png"}}`.
 */
struct MaterialTextures {
    /** RGBA base-color map; multiplied by Surface::albedo. */
    Texture albedo;

    /** Tangent-space normal map (RGB); perturbs the interpolated normal. */
    Texture normal;

    /**
     * Combined metallic (blue) / roughness (green) map, glTF-style; each
     * channel multiplies the matching Surface scalar.
     */
    Texture metallicRoughness;

    /** Emissive map (RGB); multiplied by Surface::emissive * emissiveStrength. */
    Texture emissive;
};

} // namespace Concord::Material

#endif // CONCORD_MATERIALTEXTURES_H
