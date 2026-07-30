#ifndef CONCORD_RENDERWATERSURFACE_H
#define CONCORD_RENDERWATERSURFACE_H

#include "engine/water/WaterSurfaceDesc.h"
#include "engine/water/WaterSurfaceState.h"
#include "engine/water/WaterWave.h"

#include <cstdint>

namespace Concord {

/**
 * Maximum number of water surfaces the water pass draws for one view.
 *
 * Surfaces are whole bodies of water rather than props, so a handful covers a
 * level; anything past this is dropped with a diagnostic instead of silently
 * costing a pass. Kept in lock-step with the water shader's uniform arrays.
 */
inline constexpr std::uint32_t kMaxRenderWaterSurfaces = 8;

/**
 * The render thread's flat, resolved form of one water surface.
 *
 * Object::WaterBody owns the authored description and the animation clock; this
 * is the backend-agnostic POD the Scene gathers each frame and the render thread
 * turns into a tessellated grid draw. Like RenderInstance and RenderSmokeVolume
 * it carries no identity or ownership — just "this surface, this frame".
 *
 * `waves` is the CPU-sampled resolve (kept for buoyancy parity); the bake
 * shader may instead re-expand the runtime wind spectrum from `state`. `state`
 * carries the per-frame values that do not belong in authored content.
 */
struct RenderWaterSurface {
    /** Column-major 4x4 world matrix of the surface centre; the plane is local y = 0. */
    float world[16]{1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f};

    /** Local extent along X and Z, in world units. */
    float width = 40.0f;
    float length = 40.0f;

    /** Resolved wave octaves (CPU path); an empty set means a mirror-flat surface. */
    Water::WaveSet waves{};

    /** Current velocity in local XZ, world units per second. */
    float flowVelocity[2]{0.0f, 0.0f};

    /** Grid subdivisions along the longer axis (grid path; ignored by clipmap). */
    std::uint32_t tessellation = 96;

    /** Authored depth in world units, driving absorption and shoreline foam. */
    float depth = 4.0f;

    /** Packed 0xRRGGBBAA (sRGB) endpoints of the depth gradient. */
    std::uint32_t shallowColor = 0x4FA3B4ffu;
    std::uint32_t deepColor = 0x0B2A3Affu;

    float absorption = 0.35f;
    float roughness = 0.06f;
    float refractionStrength = 0.35f;
    float foamWidth = 0.6f;
    float foamIntensity = 0.8f;

    /** Mirrors the authored kind/motion so the pass can pick its shading path. */
    Water::WaterKind kind = Water::WaterKind::Lake;
    Water::WaterMotion motion = Water::WaterMotion::Dynamic;

    /** Whether this surface asked for the planar reflection pass. */
    bool planarReflection = true;

    /** Runtime-only values resolved for this frame. */
    Water::WaterSurfaceState state{};
};

} // namespace Concord

#endif // CONCORD_RENDERWATERSURFACE_H
