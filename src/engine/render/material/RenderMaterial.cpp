#include "engine/render/material/RenderMaterial.h"

#include "engine/material/MaterialDesc.h"
#include "engine/render/texture/TextureRegistry.h"

#include <algorithm>
#include <cmath>

namespace Concord {

RenderMaterial ResolveMaterial(const Material::MaterialDesc& desc) noexcept
{
    RenderMaterial out;
    out.albedo = desc.surface.albedo;
    out.emissive = desc.surface.emissive;
    out.metallic = desc.surface.metallic;
    out.roughness = desc.surface.roughness;
    out.reflectivity = std::isfinite(desc.surface.reflectivity)
        ? std::clamp(desc.surface.reflectivity, 0.0f, 1.0f) : 1.0f;
    out.emissiveStrength = desc.surface.emissiveStrength;
    out.lit = desc.model == Material::MaterialModel::Lit;

    // Intern each named map to a stable id (empty path -> TextureId::None);
    // the render thread's cache turns these back into shared GPU textures.
    out.albedoMap = TextureRegistry::Acquire(desc.textures.albedo.path);
    out.normalMap = TextureRegistry::Acquire(desc.textures.normal.path);
    out.metallicRoughnessMap = TextureRegistry::Acquire(desc.textures.metallicRoughness.path);
    out.emissiveMap = TextureRegistry::Acquire(desc.textures.emissive.path);

    out.depthTest = desc.draw.depthTest;
    out.depthWrite = desc.draw.depthWrite;
    out.cull = desc.draw.cull;
    out.blend = desc.draw.blend;
    out.priority = desc.draw.priority;
    out.planarReflection = desc.planarReflection;

    out.gradient = desc.gradient.enabled;
    if (out.gradient) {
        // The gradient's "from" end drives the base color; the surface albedo
        // is set aside while a gradient is active so both ends read from the
        // dedicated gradient colors rather than being silently overridden.
        out.albedo = desc.gradient.from;
        out.gradientTo = desc.gradient.to;
        switch (desc.gradient.axis) {
            case Material::GradientAxis::X: out.gradientAxis = 0; break;
            case Material::GradientAxis::Y: out.gradientAxis = 1; break;
            case Material::GradientAxis::Z: out.gradientAxis = 2; break;
        }
    }
    return out;
}

} // namespace Concord
