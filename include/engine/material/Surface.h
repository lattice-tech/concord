#ifndef CONCORD_SURFACE_H
#define CONCORD_SURFACE_H

#include "color/Color.h"

#include <cstdint>

namespace Concord::Material {

/**
 * The physically-inspired parameters that describe how a surface reflects
 * light: a metallic/roughness workflow, the same one used by glTF and most
 * modern real-time renderers.
 *
 * These are only consulted under MaterialModel::Lit; an Unlit material emits
 * `albedo` (or the gradient) directly. A plain aggregate so a caller writes
 * only the fields it cares about, e.g.
 * `.surface = {.albedo = COLOR_ORANGE, .metallic = 1.0f, .roughness = 0.2f}`.
 */
struct Surface {
    /**
     * Base (diffuse/albedo) color, packed 0xRRGGBBAA. For metals this is the
     * specular tint; for dielectrics it is the diffuse color.
     */
    std::uint32_t albedo = COLOR_WHITE;

    /**
     * 0 = dielectric (plastic, wood, stone), 1 = raw metal. Values in between
     * are rarely physical but useful for blends. Drives how much of `albedo`
     * tints reflections versus diffuse.
     */
    float metallic = 0.0f;

    /**
     * 0 = perfectly smooth (sharp, mirror-like highlight), 1 = fully rough
     * (broad, dim highlight). Controls the size and intensity of speculars.
     */
    float roughness = 0.5f;

    /**
     * Strength of explicit scene-reflection sources, in the range [0, 1].
     * This scales real-time cubemap and planar reflections without changing
     * the metallic/roughness BRDF used for direct lighting.
     */
    float reflectivity = 1.0f;

    /** Self-illumination color, packed 0xRRGGBBAA; scaled by `emissiveStrength`. */
    std::uint32_t emissive = COLOR_BLACK;

    /**
     * Multiplier on `emissive`. 0 disables emission; values above 1 push the
     * surface brighter than any light could (useful once bloom exists).
     */
    float emissiveStrength = 0.0f;
};

} // namespace Concord::Material

#endif // CONCORD_SURFACE_H
