#ifndef CONCORD_MATERIALMODEL_H
#define CONCORD_MATERIALMODEL_H

namespace Concord::Material {

/**
 * How a surface converts its parameters into a final pixel color.
 *
 * This is the top-level switch a material carries: it decides whether the
 * lighting rig touches the surface at all. Every other material parameter
 * (metallic, roughness, gradient, textures) is interpreted in the context of
 * the chosen model. New models (e.g. a toon/cel ramp) are added here and to
 * the shading pass without disturbing existing descriptors.
 */
enum class MaterialModel {
    /**
     * No lighting: the resolved base color is emitted verbatim. Useful for
     * UI, debug shapes, skyboxes and anything that should ignore the scene's
     * lights. metallic/roughness are ignored; gradient and emissive still apply.
     */
    Unlit,

    /**
     * Physically-inspired lighting: the surface reacts to the scene's lights
     * using its metallic and roughness parameters (see Surface). This is the
     * default for solid objects.
     */
    Lit,
};

/** Canonical, human-readable name of a MaterialModel (never null). */
inline const char* ToString(MaterialModel model) noexcept
{
    switch (model) {
        case MaterialModel::Unlit: return "Unlit";
        case MaterialModel::Lit:   return "Lit";
    }
    return "Lit";
}

} // namespace Concord::Material

#endif // CONCORD_MATERIALMODEL_H
