#ifndef CONCORD_WATERSURFACEDESC_H
#define CONCORD_WATERSURFACEDESC_H

#include "engine/object/Transform.h"
#include "engine/water/WaterWave.h"

#include <cstdint>

namespace Concord::Water {

/** Which body of water a surface represents; selects the authoring defaults. */
enum class WaterKind : std::uint8_t {
    /** A closed body: waves travel outward-ish and there is no net current. */
    Lake = 0,
    /** A channel: the surface has a dominant flow direction along its length. */
    River = 1,
    /** An open ocean: the widest, most wind-driven surface, far from any shore. */
    Ocean = 2,
};

/** Whether the surface animates at all. */
enum class WaterMotion : std::uint8_t {
    /**
     * Mirror-still: no wave displacement and no scrolling. The cheapest option,
     * and the right one for a reflective pond or a stylised look.
     */
    Still = 0,
    /** Animated: a wind-driven spectrum (Gerstner fall-back if no wind is set). */
    Dynamic = 1,
};

/**
 * @brief Render-time optical parameters beyond the legacy absorption/roughness set.
 *
 * These mirror Crest's per-quad data the fragment shader needs to do its
 * high-end composite: a deep/scattering tint, sub-surface scattering strength,
 * and a foam fingerprint. Defaults track the legacy look so a desc authored
 * against the old fields still renders close to its previous appearance.
 */
struct WaterOptics {
    /** Sub-surface scattering strength in [0, 1]; 0 disables the SSS term. */
    float subsurfaceScattering = 0.45f;

    /**
     * Tint added to light that has scattered inside the column, packed 0xRRGGBBAA
     * (sRGB). Default is a cyan-leaning "ocean interior" colour.
     */
    std::uint32_t scatteringColor = 0x0E4A60ffu;

    /** Procedural foam grain scale; higher = finer foam detail. */
    float foamGrainScale = 1.0f;

    /** Sun specular anisotropy in [0, 1]; brightens the sun path on rough water. */
    float sunGlintIntensity = 1.0f;
};

/**
 * @brief Everything a water surface is authored from.
 *
 * The surface is a rectangle in the node's local XZ plane, centred on the
 * node's origin at local y = 0, so the node transform places, rotates and
 * scales it like any other object. Depth is authored rather than derived from
 * scene geometry: it drives how quickly the water absorbs light, which is what
 * makes a shallow stream read as clear and a lake as deep.
 *
 * Two authoring paths drive the waves: the original Gerstner-octave fields
 * (kept for backwards compatibility) and the wind-driven spectrum (WindState,
 * which the bake shader expands). If `wind.windSpeed > 0` and the surface is
 * dynamic, the spectrum path wins and the Gerstner fields become a fallback.
 */
struct WaterSurfaceDesc {
    /** Placement of the surface's centre; the water plane is local y = 0. */
    Transform transform{};

    /** Extent along local X, in world units. */
    float width = 40.0f;

    /** Extent along local Z, in world units. */
    float length = 40.0f;

    WaterKind kind = WaterKind::Lake;
    WaterMotion motion = WaterMotion::Dynamic;

    /**
     * Grid subdivisions along the longer axis. Wave displacement happens per
     * vertex, so this is the ceiling on wave detail; 1 metre per quad is a good
     * starting point for walkable water. (Ignored by the clipmap renderer,
     * which sizes its own mesh around the viewer; retained for the grid path.)
     */
    std::uint32_t tessellation = 96;

    /** Water depth in world units, used for light absorption and shoreline foam. */
    float depth = 4.0f;

    /** Colour where the water is thin, packed 0xRRGGBBAA (sRGB). */
    std::uint32_t shallowColor = 0x4FA3B4ffu;

    /** Colour the water tends to at full `depth`, packed 0xRRGGBBAA (sRGB). */
    std::uint32_t deepColor = 0x0B2A3Affu;

    /** Extinction per world unit of depth; higher water hides its bed sooner. */
    float absorption = 0.35f;

    /** Microfacet roughness of the surface; small values give a sharp sun glint. */
    float roughness = 0.06f;

    /** How much the surface bends what is seen through it, 0 disables refraction. */
    float refractionStrength = 0.35f;

    /** Number of summed Gerstner octaves (legacy path), clamped to kMaxWaterWaves. */
    std::uint32_t waveCount = 4;

    /** Height of the dominant swell in world units (legacy Gerstner path). */
    float waveAmplitude = 0.12f;

    /** Crest spacing of the dominant swell in world units (legacy Gerstner path). */
    float waveLength = 7.0f;

    /** Crest sharpness in [0, 1]; see GerstnerWave::steepness. */
    float waveSteepness = 0.55f;

    /** Crest travel speed in world units per second (legacy Gerstner path). */
    float waveSpeed = 1.1f;

    /** Wave heading in degrees, 0 = local +X, increasing toward local +Z. */
    float waveDirectionDegrees = 0.0f;

    /** Current speed in world units per second; 0 for a body with no flow. */
    float flowSpeed = 0.0f;

    /** Current heading in degrees, using the same convention as the waves. */
    float flowDirectionDegrees = 0.0f;

    /** Width of the foam band along shorelines and obstacles, in world units. */
    float foamWidth = 0.6f;

    /** Foam opacity in [0, 1]; 0 disables the foam band. */
    float foamIntensity = 0.8f;

    /**
     * Whether the surface takes part in planar reflection. A still surface
     * gains the most from it; choppy water can afford to fall back to the
     * environment reflection.
     */
    bool planarReflection = true;

    /** Wind-driven spectrum authoring. When windSpeed > 0 this replaces the Gerstner fields. */
    WindState wind{};

    /** Advanced optical terms the high-end fragment shader consumes. */
    WaterOptics optics{};
};

} // namespace Concord::Water

#endif // CONCORD_WATERSURFACEDESC_H