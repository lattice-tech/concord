#ifndef CONCORD_DAE_MATERIALRESOLVER_H
#define CONCORD_DAE_MATERIALRESOLVER_H

#include "engine/asset/import/dae/XmlNode.h"
#include "engine/material/MaterialDesc.h"

#include <string>
#include <string_view>
#include <vector>

namespace Concord::Asset::Dae {

/**
 * One parsed `<effect>`: its id (matched by `<material instance_effect="#id">`)
 * and the Concord material descriptor built from its shading model.
 */
struct EffectEntry {
    std::string id;
    Material::MaterialDesc desc;
};

/**
 * One parsed `<material>`: its id and the effect id it instantiates. Collada
 * separates materials from effects so a material can be re-bound without
 * touching the effect definition.
 */
struct MaterialEntry {
    std::string id;
    std::string effectId;
};

/**
 * Resolves Collada material references to Concord material descriptors.
 *
 * Collada's material indirection is three layers deep:
 *   `<library_materials>` `<material id="M" instance_effect="#E"/>`
 *   `<library_effects>`   `<effect id="E"><profile_COMMON><technique>...`
 *   `<library_images>`    `<image><init_from>texture.png</init_from></image>`
 *
 * This class loads all three libraries once, then answers Resolve(materialId)
 * for each sub-mesh. The diffuse channel maps to albedo, shininess maps to
 * roughness (inverted), and a diffuse texture maps to the albedo texture.
 * Collada predates PBR, so metallic defaults to 0 (dielectric).
 */
class DaeMaterialResolver {
public:
    /** Loads effects, materials and images from the parsed document root. */
    void Load(const XmlNode& root, const std::string& dir);

    /**
     * Resolves a material id (the target of `<instance_material target="#M">`,
     * or a bare symbol when no bind_material is present) to its descriptor.
     * Returns a neutral default when the id is not found.
     */
    Material::MaterialDesc Resolve(std::string_view materialId) const noexcept;

private:
    std::vector<EffectEntry> m_effects;
    std::vector<MaterialEntry> m_materials;
};

} // namespace Concord::Asset::Dae

#endif // CONCORD_DAE_MATERIALRESOLVER_H
