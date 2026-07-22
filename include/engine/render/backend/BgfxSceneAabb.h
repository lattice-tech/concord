#ifndef CONCORD_BGFXSCENEAABB_H
#define CONCORD_BGFXSCENEAABB_H

#include "engine/render/frame/RenderLight.h"

#include <array>
#include <cstdint>

namespace Concord::RenderDetail {

/**
 * Transforms the 8 corners of a local-space AABB by a row-major 4x4 world
 * matrix and returns the world-space AABB that bounds them, in `outMin`/`outMax`.
 *
 * Used to fit the directional-light shadow frustum around everything visible
 * this frame without reading GPU vertex data back: each mesh carries its
 * local-space AABB stashed at upload time (see `BgfxMeshStore::Create`), so
 * the per-frame world-AABB union is built entirely on the CPU.
 */
void TransformAabbWorld(float outMin[3], float outMax[3],
                        const float localMin[3], const float localMax[3],
                        const float worldMatrix[16]) noexcept;

/** Transforms per-bone local bounds through a palette and then the world matrix. */
void TransformSkinnedAabbWorld(float outMin[3], float outMax[3],
                               const std::array<float, 6>* boneAabbs,
                               std::uint32_t boneAabbCount,
                               const float* bonePalette,
                               std::uint32_t boneCount,
                               const float worldMatrix[16]) noexcept;

/**
 * Returns the index of the first directional light flagged for shadow casting,
 * or -1 if none. Only one shadow caster is supported per frame (the shader's
 * `u_lightViewProj` describes a single light); the first one wins, mirroring
 * the "single shadow-casting sun" model the lighting path implements.
 */
int FindShadowCaster(const RenderLight* lights, std::uint32_t count) noexcept;

} // namespace Concord::RenderDetail

#endif // CONCORD_BGFXSCENEAABB_H
