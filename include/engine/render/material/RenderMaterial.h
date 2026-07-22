#ifndef CONCORD_RENDERMATERIAL_H
#define CONCORD_RENDERMATERIAL_H

#include "Concord/CExport.h"
#include "engine/material/BlendMode.h"
#include "engine/render/material/CullMode.h"
#include "engine/render/material/DepthTest.h"
#include "engine/render/texture/TextureId.h"

#include <cstdint>

namespace Concord::Material {
struct MaterialDesc;
} // namespace Concord::Material

namespace Concord {

/**
 * The render thread's compact, resolved form of a material.
 *
 * Material::MaterialDesc is the rich, caller-facing description (colors,
 * enums, texture paths, std::string). This is what survives the trip to the
 * GPU: a flat POD with no ownership, packed one-per-instance into the mesh
 * shader's instance-data buffer. Keeping it separate lets the descriptor grow
 * new conveniences without widening the per-instance payload, and keeps the
 * backend free of any std::string / file-path concerns.
 *
 * Colors stay packed 0xRRGGBBAA here; the backend unpacks them to float4 at
 * pack time (see BgfxRenderBackend). Produced by ResolveMaterial.
 */
struct RenderMaterial {
    /** Base color (and the gradient's "from" end when `gradient` is set). */
    std::uint32_t albedo = 0xffffffffu;

    /** Gradient's "to" end; only meaningful when `gradient` is true. */
    std::uint32_t gradientTo = 0xffffffffu;

    /** Emissive color, pre-strength; scaled by `emissiveStrength` in the shader. */
    std::uint32_t emissive = 0x000000ffu;

    /** Metallic response, 0..1 (see Material::Surface). */
    float metallic = 0.0f;

    /** Roughness response, 0..1 (see Material::Surface). */
    float roughness = 0.5f;

    /** Multiplier for explicit real-time cubemap and planar reflections. */
    float reflectivity = 1.0f;

    /** Multiplier applied to `emissive`. */
    float emissiveStrength = 0.0f;

    /** True to light the surface, false to emit the resolved color unlit. */
    bool lit = true;

    /** True to blend `albedo`->`gradientTo` across `gradientAxis`. */
    bool gradient = false;

    /** Local axis the gradient runs along: 0 = X, 1 = Y, 2 = Z. */
    int gradientAxis = 1;

    /** Base-color map; TextureId::None means "use `albedo`". */
    TextureId albedoMap = TextureId::None;

    /** Tangent-space normal map; TextureId::None means "use the geometric normal". */
    TextureId normalMap = TextureId::None;

    /** Metallic/roughness map; TextureId::None means "use `metallic`/`roughness`". */
    TextureId metallicRoughnessMap = TextureId::None;

    /** Emissive map; TextureId::None means "use `emissive`". */
    TextureId emissiveMap = TextureId::None;

    /** Depth comparison for this draw (see Material::DrawOptions). */
    DepthTest depthTest = DepthTest::LessEqual;

    /** Whether this draw writes depth. */
    bool depthWrite = true;

    /** Which faces this draw culls. */
    CullMode cull = CullMode::Back;

    /**
     * How this draw composites with the frame buffer. Non-Opaque marks the
     * draw transparent: the backend orders it after opaque geometry and drops
     * its depth write (see BgfxRenderBackend::RenderView).
     */
    Material::BlendMode blend = Material::BlendMode::Opaque;

    /** Draw-order bias; lower renders first, higher renders on top. */
    int priority = 0;

    /** Sample the window's planar reflection map (see MaterialDesc). */
    bool planarReflection = false;
};

/**
 * Whole-value equality of two resolved materials.
 *
 * Two materials that compare equal set exactly the same shading uniforms
 * and render state, so the render batcher collapses them into one instanced
 * submit (see engine/render/batch/RenderBatcher). The comparison is
 * field-by-field on the POD's bit patterns; floats are compared with `==`,
 * which is well defined here because ResolveMaterial never produces a
 * signalling NaN or a mixed signed zero, so every value is canonical.
 */
inline bool operator==(const RenderMaterial& a, const RenderMaterial& b) noexcept
{
    return a.albedo == b.albedo
        && a.gradientTo == b.gradientTo
        && a.emissive == b.emissive
        && a.metallic == b.metallic
        && a.roughness == b.roughness
        && a.reflectivity == b.reflectivity
        && a.emissiveStrength == b.emissiveStrength
        && a.lit == b.lit
        && a.gradient == b.gradient
        && a.gradientAxis == b.gradientAxis
        && a.albedoMap == b.albedoMap
        && a.normalMap == b.normalMap
        && a.metallicRoughnessMap == b.metallicRoughnessMap
        && a.emissiveMap == b.emissiveMap
        && a.depthTest == b.depthTest
        && a.depthWrite == b.depthWrite
        && a.cull == b.cull
        && a.blend == b.blend
        && a.priority == b.priority
        && a.planarReflection == b.planarReflection;
}

/** Unequal materials — the negation of operator==. */
inline bool operator!=(const RenderMaterial& a, const RenderMaterial& b) noexcept
{
    return !(a == b);
}

/**
 * Collapses a rich MaterialDesc into the flat RenderMaterial the GPU path
 * consumes (unpacking enums into the small numeric fields above). Each named
 * texture path is interned to a TextureId through the TextureRegistry; the
 * render thread's texture cache resolves those ids to GPU textures. An empty
 * path resolves to TextureId::None so the channel falls back to its flat value.
 */
CENGINE_API RenderMaterial ResolveMaterial(const Material::MaterialDesc& desc) noexcept;

} // namespace Concord

#endif // CONCORD_RENDERMATERIAL_H
