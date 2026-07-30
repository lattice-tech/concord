#include "engine/water/WaterWave.h"

#include <algorithm>
#include <cmath>

namespace Concord::Water {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kMinWavelength = 0.05f;
/** Standard gravity, metres/second^2. */
constexpr float kGravity = 9.81f;

bool IsUsable(const GerstnerWave& wave) noexcept
{
    return std::isfinite(wave.amplitude) && std::isfinite(wave.wavelength)
        && std::isfinite(wave.steepness) && std::isfinite(wave.speed)
        && std::isfinite(wave.directionX) && std::isfinite(wave.directionZ)
        && wave.wavelength >= kMinWavelength && wave.amplitude > 0.0f;
}

/** Unit travel direction, falling back to +X for a degenerate one. */
void Direction(const GerstnerWave& wave, float& outX, float& outZ) noexcept
{
    const float length = std::sqrt(wave.directionX * wave.directionX
                                   + wave.directionZ * wave.directionZ);
    if (length <= 1.0e-6f) {
        outX = 1.0f;
        outZ = 0.0f;
        return;
    }
    outX = wave.directionX / length;
    outZ = wave.directionZ / length;
}

/**
 * Deep-water phase speed from the dispersion relation: ω² = g·k ⇒ c = ω/k = sqrt(g/k) = sqrt(g·λ/2π).
 * The GPU bake uses the same relation so CPU and GPU keep the same swell timing.
 */
float DispersionSpeed(float wavelength) noexcept
{
    return std::sqrt(kGravity * wavelength / (2.0f * kPi));
}

} // namespace

WaveSet BuildWaveSet(std::uint32_t count, float amplitude, float wavelength,
                     float steepness, float speed, float directionDegrees) noexcept
{
    WaveSet set;
    if (count == 0 || !std::isfinite(amplitude) || !std::isfinite(wavelength)
        || !std::isfinite(steepness) || !std::isfinite(speed)
        || !std::isfinite(directionDegrees) || amplitude <= 0.0f
        || wavelength < kMinWavelength) {
        return set;
    }

    set.count = std::min(count, kMaxWaterWaves);
    const float clampedSteepness = std::clamp(steepness, 0.0f, 1.0f);
    float octaveAmplitude = amplitude;
    float octaveWavelength = wavelength;
    // Fan successive octaves off the authored heading. 37 degrees is chosen so
    // the octaves neither align (which would beat into one big wave) nor sit at
    // right angles (which reads as a grid).
    constexpr float kOctaveFanDegrees = 37.0f;
    for (std::uint32_t index = 0; index < set.count; ++index) {
        const float heading =
            (directionDegrees + kOctaveFanDegrees * static_cast<float>(index)) * kDegToRad;
        GerstnerWave& wave = set.waves[index];
        wave.amplitude = octaveAmplitude;
        wave.wavelength = std::max(octaveWavelength, kMinWavelength);
        wave.steepness = clampedSteepness;
        // Deep-water waves are dispersive: shorter waves travel slower, so speed
        // scales with the square root of the wavelength ratio.
        wave.speed = speed * std::sqrt(wave.wavelength / wavelength);
        wave.directionX = std::cos(heading);
        wave.directionZ = std::sin(heading);
        octaveAmplitude *= 0.5f;
        octaveWavelength *= 0.5f;
    }
    return set;
}

WaveSet BuildWaveSetFromWind(const WindState& wind) noexcept
{
    WaveSet set;
    if (!std::isfinite(wind.windSpeed) || wind.windSpeed <= 0.0f
        || !std::isfinite(wind.choppiness) || wind.choppiness < 0.0f
        || !std::isfinite(wind.amplitudeScale) || wind.amplitudeScale <= 0.0f) {
        return set;
    }

    // Pierson-Moskowitz significant wave height: Hs = 0.21·U²/g (metres).
    const float significantHeight = 0.21f * wind.windSpeed * wind.windSpeed / kGravity;
    // Peak wavelength from the PM period Tp ≈ 0.81·U (fully developed) ⇒ λp = g·Tp²/2π.
    const float peakPeriod = 0.81f * wind.windSpeed;
    const float peakWavelength = kGravity * peakPeriod * peakPeriod / (2.0f * kPi);
    if (significantHeight <= 0.0f || !(peakWavelength > kMinWavelength)) {
        return set;
    }

    // Pick four octaves spanning the spectrum's energy band. Energy falls off as
    // a Phillips curve on either side of the peak, so the dominant wave is at
    // the peak and the rest are progressively shorter / lower in amplitude.
    // Keeping four entries lets the existing WaveSet sampling (and the tests
    // that count it) continue to work, while still approximating the sea state.
    set.count = kMaxWaterWaves;
    const float heading = wind.windDirectionDegrees * kDegToRad;
    const float spreadRad = std::clamp(wind.spreadDegrees, 1.0f, 90.0f) * kDegToRad;
    for (std::uint32_t index = 0; index < set.count; ++index) {
        // Wavelength ratio: 1, ~0.55, ~0.30, ~0.16 — spread like the upper tail.
        const float ratio = std::pow(0.55f, static_cast<float>(index));
        const float wavelength = std::max(peakWavelength * ratio, kMinWavelength);
        // Phillips amplitude falloff: a ~ g²/k^4 scaled into practical units.
        // Normalised so the dominant (index 0) wave's amplitude is Hs/4.
        const float k = 2.0f * kPi / wavelength;
        const float phillips = std::exp(-kMinWavelength / wavelength)
            / std::max(k * k * k * k, 1.0e-6f);
        const float dominantAmplitude = significantHeight * 0.25f * wind.amplitudeScale;
        const float amplitudeFalloff = std::sqrt(phillips
            / (std::exp(-kMinWavelength / peakWavelength)
               / std::max(2.0f * kPi / peakWavelength, 1.0e-6f)));
        const float amplitude = std::max(std::min(dominantAmplitude * amplitudeFalloff,
                                                  dominantAmplitude * 2.5f),
                                         1.0e-4f);
        // Direction spreads around the wind: the dominant wave (index 0) sits
        // exactly on the wind heading so its direction reads as the sea's mean
        // travel; the rest fan symmetrically out to ±spread around it.
        float dirOffset = 0.0f;
        if (index > 0) {
            const float fanIndex = static_cast<float>(index)
                - (static_cast<float>(set.count) - 1.0f) * 0.5f;
            dirOffset = fanIndex * (spreadRad / static_cast<float>(set.count));
        }
        GerstnerWave& wave = set.waves[index];
        wave.amplitude = amplitude;
        wave.wavelength = wavelength;
        wave.steepness = std::clamp(wind.choppiness, 0.0f, 1.0f);
        wave.speed = DispersionSpeed(wavelength);
        wave.directionX = std::cos(heading + dirOffset);
        wave.directionZ = std::sin(heading + dirOffset);
    }
    return set;
}

SurfacePoint SampleSurface(const WaveSet& waves, float x, float z,
                           float time) noexcept
{
    SurfacePoint point;
    point.position = Vector3{x, 0.0f, z};
    point.normal = Vector3{0.0f, 1.0f, 0.0f};
    if (waves.count == 0 || !std::isfinite(x) || !std::isfinite(z)
        || !std::isfinite(time)) {
        return point;
    }

    const std::uint32_t count = std::min(waves.count, kMaxWaterWaves);
    float normalX = 0.0f;
    float normalY = 1.0f;
    float normalZ = 0.0f;
    for (std::uint32_t index = 0; index < count; ++index) {
        const GerstnerWave& wave = waves.waves[index];
        if (!IsUsable(wave)) {
            continue;
        }
        float dirX = 1.0f;
        float dirZ = 0.0f;
        Direction(wave, dirX, dirZ);

        const float k = 2.0f * kPi / wave.wavelength;
        const float phase = k * (dirX * x + dirZ * z) - k * wave.speed * time;
        const float cosPhase = std::cos(phase);
        const float sinPhase = std::sin(phase);
        // Q normalised by k * amplitude * count keeps the total horizontal pull
        // below the point where crests would fold over themselves.
        const float ka = k * wave.amplitude;
        const float q = wave.steepness
            / std::max(ka * static_cast<float>(count), 1.0e-4f);

        point.position.x += q * wave.amplitude * dirX * cosPhase;
        point.position.z += q * wave.amplitude * dirZ * cosPhase;
        point.position.y += wave.amplitude * sinPhase;

        // Analytic normal of the same sum (GPU Gems 1, ch. 1): exact for the
        // displaced surface, and free of the seams finite differences leave
        // between mesh tiles.
        normalX -= dirX * ka * cosPhase;
        normalZ -= dirZ * ka * cosPhase;
        normalY -= q * ka * sinPhase;
    }

    const float length =
        std::sqrt(normalX * normalX + normalY * normalY + normalZ * normalZ);
    if (length > 1.0e-6f && std::isfinite(length)) {
        point.normal = Vector3{normalX / length, normalY / length, normalZ / length};
    }
    if (!std::isfinite(point.position.x) || !std::isfinite(point.position.y)
        || !std::isfinite(point.position.z)) {
        point.position = Vector3{x, 0.0f, z};
        point.normal = Vector3{0.0f, 1.0f, 0.0f};
    }
    return point;
}

float SampleHeight(const WaveSet& waves, float x, float z, float time) noexcept
{
    return SampleSurface(waves, x, z, time).position.y;
}

} // namespace Concord::Water