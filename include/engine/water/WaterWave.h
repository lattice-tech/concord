#ifndef CONCORD_WATERWAVE_H
#define CONCORD_WATERWAVE_H

#include "Concord/CExport.h"
#include "math/Vector3.h"

#include <array>
#include <cstdint>

namespace Concord::Water {

/**
 * Compact authoring handle kept for backwards compatibility with the original
 * Gerstner-octave set; the spectrum authoring path (WindState) is the
 * high-quality entry point new code should prefer.
 */
inline constexpr std::uint32_t kMaxWaterWaves = 4;

/**
 * @brief One Gerstner (trochoidal) wave travelling across the XZ plane.
 *
 * Retained for backwards compatibility with code that authored waves one octave
 * at a time. The spectrum-driven authoring path (WindState) supersedes it for
 * new fidelity work; the cascade bake still consumes the same shape but now
 * also accepts spectrum parameters through SurfaceWeather (see WaterSurfaceDesc).
 */
struct GerstnerWave {
    /** Crest-to-mean height in world units. */
    float amplitude = 0.15f;

    /** Distance between crests in world units; must stay > 0. */
    float wavelength = 6.0f;

    /**
     * Crest sharpness in [0, 1]. Normalised by wavelength, amplitude and
     * wave count before use, so 1 is the steepest shape that still cannot fold
     * the surface back over itself.
     */
    float steepness = 0.6f;

    /** Crest travel speed in world units per second. */
    float speed = 1.2f;

    /** Travel direction on the XZ plane; normalised on use, zero falls back to +X. */
    float directionX = 1.0f;
    float directionZ = 0.0f;
};

/** A fixed-capacity set of waves summed into one surface. */
struct WaveSet {
    std::array<GerstnerWave, kMaxWaterWaves> waves{};

    /** Number of leading entries in `waves` that participate; 0 is a flat surface. */
    std::uint32_t count = 0;
};

/**
 * @brief Wind-driven sea state, the Crest-style authoring surface.
 *
 * A handful of numbers describes the whole ocean spectrum: the wind sets the
 * dominant wavelength and direction, its fetch/speed the energy, and the
 * choppiness how much the crests sharpen. The bake shader (fs_water_bake)
 * expands this into a directional octave spectrum with a Phillips falloff and
 * an angular spread, which is what produces the irregular, never-repeating sea
 * that distinguishes a real ocean from four sines.
 *
 * All quantities are physical-ish; the constants are chosen so that a wind of
 * 8 m/s reads as a calm day and 20 m/s as a rough one, with the swell aligned to
 * the wind by default.
 */
struct WindState {
    /** Wind speed in metres/second. Sets the spectrum energy (significant wave height ~ 0.21·U²/g). */
    float windSpeed = 8.0f;

    /** Wind heading in degrees, 0 = +X, increasing toward +Z. Swell follows it by default. */
    float windDirectionDegrees = 0.0f;

    /**
     * Fetch length in metres. Larger fetch grows longer swells at the same wind;
     * a small fetch reads as a local choppy sea with no organised swell.
     */
    float fetchMeters = 2000.0f;

    /** Choppiness multiplier in [0, 1.7]. Above 1 crests sharpen toward breaking. */
    float choppiness = 1.0f;

    /** Vertical scale applied to the whole displacement, for authoring reach. */
    float amplitudeScale = 1.0f;

    /**
     * Angular spread of the spectrum around the wind direction in degrees.
     * Half-spread; 60 gives the broad, directionless look of open water; a
     * channel narrows this to ~15 so the sea runs in lanes.
     */
    float spreadDegrees = 55.0f;
};

/** A displaced surface point with its analytic normal. */
struct SurfacePoint {
    /** World-space (or surface-local) position after wave displacement. */
    Vector3 position{};

    /** Unit normal derived from the same wave sum, not from finite differences. */
    Vector3 normal{0.0f, 1.0f, 0.0f};
};

/**
 * @brief Builds a wave set from one authored swell, fanned into octaves.
 *
 * Backwards-compatible with the original octave-fan authoring; the bake shader
 * no longer reads this (the spectrum is authored through WindState instead),
 * but CPU-side sampling and the public API still use it for buoyancy parity
 * when a caller picks the Gerstner path.
 *
 * @param directionDegrees Travel heading, 0 = +X, increasing toward +Z.
 */
CENGINE_API WaveSet BuildWaveSet(std::uint32_t count, float amplitude, float wavelength,
                     float steepness, float speed, float directionDegrees) noexcept;

/**
 * @brief Builds a wave set approximating a wind-driven sea for CPU sampling.
 *
 * The bake shader expands the same WindState into a full octave spectrum on
 * the GPU; this CPU approximation sums a representative handful of those
 * waves so buoyancy queries stay close to the rendered surface without
 * duplicating the whole spectrum. The significant wave height and dominant
 * wavelength match the spectrum's, so a buoy riding the CPU sum appears to
 * follow the same sea state as the drawn ocean.
 */
CENGINE_API WaveSet BuildWaveSetFromWind(const WindState& wind) noexcept;

/**
 * @brief Evaluates the summed wave displacement and normal at a surface point.
 *
 * @param x,z Undisplaced surface coordinates in the same space as the waves.
 * @param time Seconds since the surface started animating.
 * @return The displaced point and its unit normal; an empty wave set returns
 *         (x, 0, z) with an up normal, so a still lake is exactly flat.
 */
CENGINE_API SurfacePoint SampleSurface(const WaveSet& waves, float x, float z,
                           float time) noexcept;

/** Convenience wrapper returning only the vertical displacement (buoyancy queries). */
CENGINE_API float SampleHeight(const WaveSet& waves, float x, float z, float time) noexcept;

} // namespace Concord::Water

#endif // CONCORD_WATERWAVE_H