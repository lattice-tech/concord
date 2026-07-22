#ifndef CONCORD_RENDERSMOKEVOLUME_H
#define CONCORD_RENDERSMOKEVOLUME_H

#include <cstdint>

namespace Concord {

/**
 * Maximum number of local smoke volumes the smoke pass composites in one draw.
 *
 * Kept in lock-step with `MAX_SMOKE` in `fs_smoke_march.sc`: the backend uploads
 * at most this many boxes per view as uniform arrays; volumes beyond it are
 * dropped with a diagnostic. Raising it means editing both places and
 * rebuilding the shaders.
 */
inline constexpr std::uint32_t kMaxRenderSmokeVolumes = 8;

/** Falloff shape used to carve a soft boundary out of the volume's box. */
enum class SmokeShape : std::uint8_t {
    Box = 0,       ///< Soft-edged box: each face fades over `edgeSoftness`.
    Ellipsoid = 1, ///< Soft-edged ellipsoid inscribed in the box (rounded puff).
};

/**
 * The render thread's flat, resolved form of one local smoke volume.
 *
 * Object::SmokeVolume produces the rich, caller-facing description; this is the
 * backend-agnostic POD the Scene gathers each frame (see Scene::Tick) and the
 * render thread packs into the smoke pass's uniform arrays. Like RenderLight /
 * RenderInstance it carries no identity or ownership — just "this volume, this
 * frame". The box is an axis-aligned bounding box already resolved to world
 * space from the node's transform. Color is packed 0xRRGGBBAA (sRGB), unpacked
 * and linearized by the shader.
 *
 * The volume is ray-marched through an animated fractal density field carved to
 * `coverage` and faded toward the boundary by `edgeSoftness`, so it reads as a
 * living, wispy puff rather than a hard box. `windOffset` is the accumulated
 * world-space scroll of that field (the node advances it from wind + buoyancy
 * each frame), which is what makes the smoke drift and roil over time.
 */
struct RenderSmokeVolume {
    /** World-space axis-aligned box minimum corner. */
    float boxMin[3]{-1.0f, -1.0f, -1.0f};

    /** World-space axis-aligned box maximum corner. */
    float boxMax[3]{1.0f, 1.0f, 1.0f};

    /** Accumulated world-space offset of the density field (wind + buoyancy). */
    float windOffset[3]{0.0f, 0.0f, 0.0f};

    /** Smoke tint, packed 0xRRGGBBAA (sRGB); alpha is ignored. */
    std::uint32_t color = 0xdcdcdcffu;

    /** Optical density (extinction scale) of the filled medium. */
    float density = 1.0f;

    /** Henyey-Greenstein scattering anisotropy in [-1, 1] (forward when > 0). */
    float anisotropy = 0.35f;

    /** World size of one base noise cell; larger is smoother/broader billows. */
    float noiseScale = 3.0f;

    /** Fraction of the box the density fills, 0..1 (higher is thicker/fuller). */
    float coverage = 0.55f;

    /** High-frequency erosion strength, 0..1 (wispier edges as it rises). */
    float detail = 0.5f;

    /** Boundary fade fraction, 0..1 (how far in from the faces smoke dissolves). */
    float edgeSoftness = 0.4f;

    /** Self-emission scale added before absorption (0 = purely lit smoke). */
    float emissive = 0.0f;

    /** Falloff shape used to carve the soft boundary. */
    SmokeShape shape = SmokeShape::Ellipsoid;
};

} // namespace Concord

#endif // CONCORD_RENDERSMOKEVOLUME_H
