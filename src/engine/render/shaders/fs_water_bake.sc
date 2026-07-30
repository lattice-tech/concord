$input v_uv

#include <bgfx_shader.sh>

#define WATER_SPECTRUM_WAVES 48
#define WATER_PI 3.14159265358979
#define WATER_GRAVITY 9.81

// (centreX, centreZ, world extent, texel world size) of the level being baked.
uniform vec4 u_waterCascadeLevel;
// (time, choppiness, level index, level count).
uniform vec4 u_waterCascadeSpectrum;
// (amplitude, wavelength, speed, direction in degrees) of the authored swell.
uniform vec4 u_waterCascadeGerstner;
// (windSpeed, windDirection, spreadDegrees, amplitudeScale) of the wind spectrum.
uniform vec4 u_waterCascadeWind;

/**
 * Bakes one cascade level: xyz displacement in RGB, surface Jacobian in A.
 *
 * Levels are CUMULATIVE, which is the property the water vertex and fragment
 * stages rely on: level N contains every wave its own texels can represent, that
 * is its own band plus every longer band above it. Two consequences matter.
 *
 * A vertex or pixel then needs one sample, not a sum over levels, so the wave
 * height at a world position no longer depends on which level asked for it. That
 * is what lets neighbouring clipmap rings agree on the surface instead of tearing
 * a seam along every ring boundary.
 *
 * And blending between two adjacent levels becomes a smooth loss of the shortest
 * waves rather than a change of content, so distance fades detail out instead of
 * swapping one wave field for another.
 *
 * The lower cutoff is the only hard gate: a wave shorter than two texels cannot
 * be represented at this level and is dropped, which is what removes the
 * aliasing that per-vertex wave evaluation shows in the distance.
 *
 * The spectrum is a Phillips wind-wave spectrum sampled into directional
 * octaves: the dominant wavelength sets the spectral peak, choppiness sharpens
 * the crests (the horizontal Gerstner pull), and the angular spread fans the
 * octaves across the wind quadrant so the sea never repeats. Unlike a single
 * swell, the spectrum has energy across many wavelengths, which is why a real
 * ocean carries both long ground swells and short capillary chop at once.
 */
void main()
{
	vec2 centre = u_waterCascadeLevel.xy;
	float extent = u_waterCascadeLevel.z;
	float texel = max(u_waterCascadeLevel.w, 1e-4);
	float time = u_waterCascadeSpectrum.x;
	float choppiness = clamp(u_waterCascadeSpectrum.y, 0.0, 1.7);
	float levelIndex = u_waterCascadeSpectrum.z;
	float levelCount = max(u_waterCascadeSpectrum.w, 1.0);

	// World position this texel represents.
	vec2 world = centre + (v_uv - 0.5) * extent;

	// --- Authored swell (legacy Gerstner path) ---------------------------
	float swellAmplitude = max(u_waterCascadeGerstner.x, 0.0);
	float swellWavelength = max(u_waterCascadeGerstner.y, 0.05);
	float swellSpeed = u_waterCascadeGerstner.z;
	float swellHeading = radians(u_waterCascadeGerstner.w);

	// --- Wind spectrum parameters (high-quality path) -------------------
	// The wind speed in m/s sets the deep-water spectral peak via the Pierson-
	// Moskowitz relation: ωp = 0.855·g/U, so λp = 2π·g/ωp². We pack the wind
	float windSpeed = u_waterCascadeWind.x;
	float windHeading = radians(u_waterCascadeWind.y);
	float spreadDeg = u_waterCascadeWind.z;
	float ampScale = u_waterCascadeWind.w;
	bool useWind = windSpeed > 0.01;

	// Spectral peak wavelength (Pierson-Moskowitz, fully developed sea).
	float peakWavelength = useWind
		? 2.0 * WATER_PI * WATER_GRAVITY / pow(0.855 * WATER_GRAVITY / max(windSpeed, 0.1), 2.0)
		: swellWavelength;
	// Significant wave height Hs ≈ 0.21·U²/g, in metres.
	float significantHeight = useWind
		? 0.21 * windSpeed * windSpeed / WATER_GRAVITY
		: swellAmplitude * 4.0;
	// Scale the whole field by the authored amplitude so presets can trim a
	// calm look below the physical Hs without losing the spectral shape.
	float amplitudeScale = useWind ? max(ampScale, 0.0) : 1.0;

	// Angular spread around the wind/swell direction. Wider for open water
	// (directionless), tighter for a channel (laned chop). Held in radians,
	// half-spread so ±spread covers the full fan.
	float spreadRad = useWind ? clamp(radians(spreadDeg), 0.02, 1.6) : 0.7;

	vec3 displacement = vec3(0.0, 0.0, 0.0);
	// Partial derivatives of the horizontal displacement, for the Jacobian.
	float dXdX = 0.0;
	float dZdZ = 0.0;
	float dXdZ = 0.0;
	// Accumulated squared slope — a fold proxy sharper than the Jacobian alone
	// for picking the crests that actually break. We leave it folded into the
	// existing w channel (Jacobian) by mixing the two so no extra target is
	// needed: the fragment shader reads w as "how near to folding" in either case.
	float slopeSquared = 0.0;

	for (int i = 0; i < WATER_SPECTRUM_WAVES; ++i) {
		float t = float(i) / float(WATER_SPECTRUM_WAVES - 1);

		// Wavelengths span six octaves around the spectral peak. Lower t = longer
		// waves dominate the energy; higher t adds capillary chop for surface
		// detail. The mapping is non-linear so more octaves cluster near the peak
		// (where the spectrum's energy actually is) rather than spreading evenly.
		float wavelengthRatio = pow(2.0, t * 6.0 - 3.0);
		float wavelength = max(peakWavelength * wavelengthRatio, 0.05);

		// Only a low cutoff, because levels are cumulative: keep every wave this
		// level's texels can carry (at least two per wavelength) and let the
		// coarser levels keep their own copies of the long ones. Gating the top
		// end as well is what would make levels hold different content and put a
		// visible discontinuity at every ring boundary.
		float weight = smoothstep(2.0 * texel, 3.0 * texel, wavelength);
		if (weight <= 0.001) {
			continue;
		}

		// Amplitude follows a Phillips-like falloff: long waves carry the energy,
		// short ones only add detail. a ~ A/k^4加权, normalised against the peak.
		float k = 2.0 * WATER_PI / wavelength;
		float kPeak = 2.0 * WATER_PI / max(peakWavelength, 0.05);
		// Avoid the k->0 singularity; the exp guard kills the longest waves that
		// exceed the level's extent anyway.
		float phillips = exp(-1.0 / max(k * kPeak * k * kPeak, 1e-6)) / max(k * k * k * k, 1e-6);
		float phillipsPeak = exp(-1.0) / max(kPeak * kPeak * kPeak * kPeak, 1e-6);
		float amplitudeRatio = sqrt(max(phillips / max(phillipsPeak, 1e-6), 0.0));
		// The dominant wave's amplitude is Hs/4 (the spectral peak contributes a
		// quarter of significant height); octaves scale off it by the Phillips ratio.
		float dominantAmplitude = significantHeight * 0.25 * amplitudeScale;
		// Phillips drops fast; clamp the short end so capillary chop doesn't vanish
		// (it carries the glint) and the long end so ground swell stays visible.
		float amplitude = clamp(dominantAmplitude * amplitudeRatio,
		                         dominantAmplitude * 0.015,
		                         dominantAmplitude * 2.0) * weight;

		// Heading: fan octaves around the wind/swell with an irrational-ish
		// stride so the directions never line up into a cross-hatch. Two waves
		// per octave sit on opposite sides of the wind for symmetric spread.
		float baseHeading = useWind ? windHeading : swellHeading;
		float fan = (fract(float(i) * 0.618034) - 0.5) * 2.0 * spreadRad;
		float heading = baseHeading + fan;
		vec2 direction = vec2(cos(heading), sin(heading));

		// Deep-water dispersion: ω = sqrt(g·k), so c = ω/k. Long waves travel faster.
		float speed = useWind ? sqrt(WATER_GRAVITY / k)
		                      : swellSpeed * sqrt(wavelength / swellWavelength);
		float phase = k * dot(direction, world) - 2.0 * WATER_PI * (speed / max(wavelength, 0.05)) * time
			+ fract(sin(float(i) * 12.9898) * 43758.5453) * 6.2831;
		float cosPhase = cos(phase);
		float sinPhase = sin(phase);

		float ka = k * amplitude;
		// Normalised so the summed horizontal pull cannot fold the surface over
		// itself at steepness 1. Choppiness above 1 lets crests sharpen toward
		// breaking, which is where the Jacobian-driven foam comes from.
		float q = choppiness / max(ka * float(WATER_SPECTRUM_WAVES), 1e-4);
		float qa = q * amplitude;

		displacement.xz += qa * direction * cosPhase;
		displacement.y += amplitude * sinPhase;

		float slope = -qa * k * sinPhase;
		dXdX += slope * direction.x * direction.x;
		dZdZ += slope * direction.y * direction.y;
		dXdZ += slope * direction.x * direction.y;
		// Track the y-slope energy in the longest octave so far (per-wavelength
		// steepness) — a band-limited proxy for how choppy this patch is.
		slopeSquared += slope * slope;
	}

	// Jacobian of the horizontal displacement. Below 1 the surface is compressed;
	// below 0 it has folded, which is where whitecaps come from — reading it off
	// the wave field puts foam exactly where the water actually breaks instead of
	// wherever a noise texture happens to be bright. The slope term nudges w
	// closer to the fold threshold on steep crests so foam breaks a touch sooner
	// on choppy water than the Jacobian alone would show.
	float jacobian = (1.0 + dXdX) * (1.0 + dZdZ) - dXdZ * dXdZ;
	float fold = clamp(1.0 - jacobian + slopeSquared * 0.04, -0.5, 2.0);

	gl_FragColor = vec4(displacement, fold);
}
