#ifndef CONCORD_SMOKEVOLUMEDESC_H
#define CONCORD_SMOKEVOLUMEDESC_H

#include "engine/object/Transform.h"
#include "engine/render/frame/RenderSmokeVolume.h"
#include "math/Vector3.h"

#include <cstdint>

namespace Concord::Gameplay {

/**
 * Everything an Object::SmokeVolume is built from — a plain aggregate so a
 * caller names only what it needs, e.g.
 * `{.transform = {.position = {0, 2, 0}}, .halfExtents = {3, 2, 3}, .density = 1.2f}`.
 *
 * The volume is a bounded region of participating medium in world space. The
 * transform places its center (and scales it); `halfExtents` sizes the region
 * in local units before that scale. The renderer ray-marches an animated
 * fractal density field carved to `coverage` and faded toward the boundary by
 * `edgeSoftness`, drifting along `windVelocity` and rising by `buoyancy` at
 * `animationSpeed`, so the smoke looks alive rather than like a solid box.
 * Every field has a sensible default; override only what you want to shape.
 */
struct SmokeVolumeDesc {
    /** Places and scales the volume; the region is centered on this transform. */
    Transform transform{};

    /** Local half-size of the region along each axis, before the transform scale. */
    Vector3 halfExtents{2.5f, 2.5f, 2.5f};

    /** Smoke tint, packed 0xRRGGBBAA (sRGB). */
    std::uint32_t color = 0xdcdcdcffu;

    /** Optical density (extinction scale); higher is thicker/darker. */
    float density = 1.0f;

    /** Boundary shape used to carve a soft edge (box or rounded ellipsoid). */
    SmokeShape shape = SmokeShape::Ellipsoid;

    /** World size of one base noise cell; larger is smoother/broader billows. */
    float noiseScale = 3.0f;

    /** Fraction of the region the density fills, 0..1. */
    float coverage = 0.55f;

    /** High-frequency erosion strength, 0..1 (wispier detail). */
    float detail = 0.5f;

    /** Boundary fade fraction, 0..1 (how far in from the edge smoke dissolves). */
    float edgeSoftness = 0.4f;

    /** Henyey-Greenstein scattering anisotropy in [-1, 1] (forward when > 0). */
    float anisotropy = 0.35f;

    /** Self-emission scale added before absorption (0 = purely lit smoke). */
    float emissive = 0.0f;

    /** World-space drift velocity of the density field (units/second). */
    Vector3 windVelocity{0.15f, 0.0f, 0.05f};

    /** Upward drift speed of the field (units/second); makes smoke rise/roil. */
    float buoyancy = 0.35f;

    /** Multiplier on how fast the field animates (0 freezes it). */
    float animationSpeed = 1.0f;
};

} // namespace Concord::Gameplay

#endif // CONCORD_SMOKEVOLUMEDESC_H
