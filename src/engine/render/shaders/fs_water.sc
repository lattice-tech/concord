$input v_wpos, v_uv, v_surface, v_cascade, v_wave

#include <bgfx_shader.sh>

// Water fragment shader — adapted from three.js Water.js (MirrorShader).
// Source: https://github.com/mrdoob/three.js/blob/dev/examples/jsm/objects/Water.js
// License: MIT (three.js).
//
// The original samples a `normalSampler` (waternormals.jpg) four times to build
// the surface normal. Concord ships no such asset, so getNoise() replaces those
// four texture fetches with four-octave programmatic hash noise that produces a
// tangent-space normal (xy = slope, z = up), matching the original's
// `noise.xzy * vec3(1.5,1.0,1.5)` normal-perturbation contract.
//
// The planar reflection is looked up via u_waterPlanarViewProj (the mirrored
// camera this engine already runs) instead of three.js's textureMatrix, and
// getShadowMask() is replaced by 1.0.

uniform vec4 u_waterCamera;       // xyz = eye, w = refraction strength
uniform vec4 u_waterShallow;      // xyz = shallow colour (linear), w = absorption
uniform vec4 u_waterDeep;         // xyz = deep colour (linear), w = authored depth
uniform vec4 u_waterOptics;       // x = size(noise scale), y = distScale, z = foam, w = kind
uniform vec4 u_waterSunDir;       // xyz = sun direction (light travels), w = intensity
uniform vec4 u_waterSunColor;     // xyz = sun colour (linear)
uniform vec4 u_waterSkyZenith;    // xyz = zenith colour (linear), w = sky intensity
uniform vec4 u_waterSkyHorizon;   // xyz = horizon colour (linear)
uniform vec4 u_waterAmbient;      // xyz = ambient fill (linear), w = intensity
uniform vec4 u_waterDepthParams;  // x = near, y = far, z = homogeneous depth, w = scene bound
uniform vec4 u_waterAdvanced;     // x = SSS, y = foam grain, z = glint, w = scattering tint
uniform vec4 u_waterPlanarParams; // x = planar bound, y = flip V, z = cascade bound
uniform mat4 u_waterPlanarViewProj;
uniform vec4 u_waterSurface;      // x = clock, y = wave count, z = animated, w = plane height
uniform vec4 u_waterWaveA[4];     // per wave: dirX, dirZ, amplitude, wavelength
uniform vec4 u_waterWaveB[4];     // per wave: speed, steepness (normalised Q), 0, 0
uniform vec4 u_waterCascadeParams[7]; // per level: centreX, centreZ, extent, texel size

SAMPLER2D(s_waterScene, 0);
SAMPLER2D(s_waterSceneDepth, 1);
SAMPLER2D(s_waterPlanar, 2);
SAMPLER2D(s_waterCascade, 3); // baked wave cascade: RGB displacement, A fold

// --- Programmatic replacement for three.js getNoise (normalSampler fetches) -
// Returns a tangent-space normal in [-1,1]: xy = horizontal slope, z = up.
// The original summed four RGBA normal-map samples and returned `noise*0.5-1.0`;
// we synthesise the same shape from four hash-noise octaves and pack a
// tangent-space unit normal so downstream `noise.xzy * vec3(1.5,1.0,1.5)`
// perturbs the surface the way the original did.
float WaterHash(vec2 p)
{
	return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

// Gradient (Perlin-style) noise in [-1,1]. Value noise plateaus on its integer
// lattice, which shades as a field of axis-aligned squares; gradient noise is
// zero at every lattice point so no cell boundary ever lines up with a
// constant-brightness patch.
float WaterGradNoise(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = fract(p);
	vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
	float a0 = WaterHash(i) * 6.2831853;
	float a1 = WaterHash(i + vec2(1.0, 0.0)) * 6.2831853;
	float a2 = WaterHash(i + vec2(0.0, 1.0)) * 6.2831853;
	float a3 = WaterHash(i + vec2(1.0, 1.0)) * 6.2831853;
	float g0 = dot(vec2(cos(a0), sin(a0)), f);
	float g1 = dot(vec2(cos(a1), sin(a1)), f - vec2(1.0, 0.0));
	float g2 = dot(vec2(cos(a2), sin(a2)), f - vec2(0.0, 1.0));
	float g3 = dot(vec2(cos(a3), sin(a3)), f - vec2(1.0, 1.0));
	return mix(mix(g0, g1, u.x), mix(g2, g3, u.x), u.y) * 1.414;
}

// Four-octave fBm height field. Every octave rotates the sampling frame by an
// irrational angle and scrolls along its own direction, so no two octaves
// share a lattice orientation or a drift axis — the classic "tiled squares"
// look of aligned octaves cannot form.
float WaterHeightFbm(vec2 p)
{
	float time = u_waterSurface.x;
	float height = 0.0;
	float amp = 0.5;
	vec2 q = p;
	vec2 drift = vec2(0.043, 0.027) * time;
	for (int i = 0; i < 4; ++i) {
		height += WaterGradNoise(q + drift) * amp;
		// Rotate ~53.13 degrees and scale by 2.02: irrational relative to the
		// lattice, so octave grids never coincide.
		q = vec2(q.x * 1.616 - q.y * 1.212, q.x * 1.212 + q.y * 1.616);
		drift = vec2(-drift.y, drift.x) * 1.7;
		amp *= 0.5;
	}
	return height;
}

vec3 getNoise(vec2 uv)
{
	float time = u_waterSurface.x;
	// Domain warp: bend the sampling domain with a low-frequency octave so any
	// residual lattice alignment is smeared into meandering shapes.
	float warp = WaterGradNoise(uv * 0.31 + vec2(time * 0.021, -time * 0.017));
	vec2 warped = uv + vec2(warp, -warp) * 0.9;
	// Central differences of the height field give a consistent slope pair —
	// the two channels come from one surface, so the ripples read as connected
	// chop instead of two unrelated stripe patterns.
	float eps = 0.22;
	float hC = WaterHeightFbm(warped);
	float hX = WaterHeightFbm(warped + vec2(eps, 0.0));
	float hZ = WaterHeightFbm(warped + vec2(0.0, eps));
	float nx = clamp((hX - hC) / eps * 0.85, -1.0, 1.0);
	float nz = clamp((hZ - hC) / eps * 0.85, -1.0, 1.0);
	float ny = sqrt(max(1.0 - nx * nx - nz * nz, 0.0));
	return vec3(nx, ny, nz);
}

// --- Baked wave cascade -----------------------------------------------------
// The atlas holds one column per level: RGB is the summed spectrum displacement
// and A the fold amount (how close the horizontal displacement is to folding
// the surface over itself; see fs_water_bake). Levels are cumulative, so one
// sample per fragment is enough and neighbouring levels agree on the surface.
vec4 SampleCascade(vec2 wpos, float level)
{
	vec4 p = u_waterCascadeParams[int(level)];
	vec2 local = (wpos - p.xy) / max(p.z, 1e-3) + 0.5;
	vec2 uv = vec2((level + clamp(local.x, 0.004, 0.996)) / 7.0,
	               clamp(local.y, 0.004, 0.996));
	return texture2D(s_waterCascade, uv);
}

// Finest level whose extent still contains the fragment with a safety margin;
// the fractional part cross-fades into the next level so the hand-over between
// rings is a smooth loss of the shortest waves rather than a visible border.
float CascadeLevelFor(vec2 wpos)
{
	for (int i = 0; i < 6; ++i) {
		vec4 p = u_waterCascadeParams[i];
		vec2 offset = abs(wpos - p.xy);
		float frac = max(offset.x, offset.y) / max(p.z * 0.5, 1e-3);
		if (frac < 0.95) {
			return float(i) + smoothstep(0.7, 0.95, frac);
		}
	}
	return 6.0;
}

// Detail slope (xy) and fold amount (z) from the cascade, blended across the
// two levels the fragment sits between. The slope comes from central
// differences of the baked height at the level's own texel spacing, so it is
// band-limited by construction and never aliases in the distance.
vec3 CascadeDetail(vec2 wpos)
{
	float levelMix = CascadeLevelFor(wpos);
	float level = floor(levelMix);
	float blend = levelMix - level;
	vec3 result = vec3(0.0, 0.0, 0.0);
	for (int pass = 0; pass < 2; ++pass) {
		float weight = pass == 0 ? 1.0 - blend : blend;
		float lv = min(level + float(pass), 6.0);
		if (weight <= 0.001) { continue; }
		float step = max(u_waterCascadeParams[int(lv)].w, 1e-3);
		vec4 centre = SampleCascade(wpos, lv);
		float hxp = SampleCascade(wpos + vec2(step, 0.0), lv).y;
		float hxm = SampleCascade(wpos - vec2(step, 0.0), lv).y;
		float hzp = SampleCascade(wpos + vec2(0.0, step), lv).y;
		float hzm = SampleCascade(wpos - vec2(0.0, step), lv).y;
		vec2 slope = vec2(hxp - hxm, hzp - hzm) / (2.0 * step);
		result += vec3(slope, max(centre.w, 0.0)) * weight;
	}
	return result;
}

// Analytic Gerstner normal evaluated per fragment, so the swell shading is
// smooth across the grid's quads instead of faceting on vertex interpolation.
vec3 WaveNormal(vec2 pos)
{
	float time = u_waterSurface.x;
	int waveCount = int(min(u_waterSurface.y, 4.0));
	float nx = 0.0;
	float nz = 0.0;
	// Low-frequency phase jitter, shading only: four ideal Gerstner trains have
	// perfectly straight, parallel crests, so their specular response lines up
	// in evenly spaced rows. Wobbling the phase by a slow noise field bends the
	// shaded crest lines the way real wind-blown water meanders, without moving
	// the displaced geometry the buoyancy queries rely on.
	float jitter = WaterGradNoise(pos * 0.021 + vec2(time * 0.013, -time * 0.011)) * 1.9;
	if (u_waterSurface.z > 0.5) {
		for (int i = 0; i < 4; ++i) {
			if (i >= waveCount) { break; }
			vec4 wa = u_waterWaveA[i];
			vec4 wb = u_waterWaveB[i];
			float k = 6.2831853 / max(wa.w, 1e-3);
			vec2 dir = normalize(wa.xy + vec2(1e-5, 0.0));
			float c = cos(k * (dot(dir, pos) - wb.x * time) + jitter);
			nx += dir.x * k * wa.z * c;
			nz += dir.y * k * wa.z * c;
		}
	}
	return normalize(vec3(-nx, 1.0, -nz));
}

float LinearizeDepth(float deviceDepth)
{
	float nearPlane = max(u_waterDepthParams.x, 1e-4);
	float farPlane = max(u_waterDepthParams.y, nearPlane + 1e-4);
	float z = clamp(deviceDepth, 0.0, 1.0);
	return (nearPlane * farPlane) / max(farPlane - z * (farPlane - nearPlane), 1e-4);
}

vec3 UnpackScatteringTint(float packed)
{
	float value = floor(packed + 0.5);
	float r = floor(value / 65536.0);
	float g = floor(mod(value / 256.0, 256.0));
	float b = mod(value, 256.0);
	vec3 srgb = vec3(r, g, b) / 255.0;
	return pow(max(srgb, vec3_splat(0.0)), vec3_splat(2.2));
}

// --- three.js Water.js sunLight, verbatim ---------------------------------
void sunLight(const vec3 surfaceNormal, const vec3 eyeDirection, float shiny,
              float spec, float diffuse, inout vec3 diffuseColor, inout vec3 specularColor)
{
	vec3 sunDirection = -normalize(u_waterSunDir.xyz);
	vec3 reflection = normalize(reflect(-sunDirection, surfaceNormal));
	float direction = max(0.0, dot(eyeDirection, reflection));
	specularColor += pow(direction, shiny) * u_waterSunColor.xyz * spec;
	diffuseColor  += max(dot(sunDirection, surfaceNormal), 0.0) * u_waterSunColor.xyz * diffuse;
}

void main()
{
	float size = max(u_waterOptics.x, 0.0001);
	// Three.js: `vec4 noise = getNoise(worldPosition.xz * size)` then
	// `surfaceNormal = normalize(noise.xzy * vec3(1.5,1.0,1.5))`.
	// World-space coordinates drive the noise, as in the original, so the
	// ripple frequency is independent of the authored surface extent. The
	// ripple normal then perturbs the analytic Gerstner normal from the
	// vertex stage, so both the swells and the fine ripples shade.
	// Two ripple octaves: a broad swell-scale layer plus a fine close-up layer,
	// so the surface keeps detail when the camera is near it.
	float viewDist = length(u_waterCamera.xyz - v_wpos.xyz);
	vec3 noiseCoarse = getNoise(v_wpos.xz * size * 3.0);
	vec3 noiseFine = getNoise(v_wpos.xz * size * 14.0 + vec2(37.0, 91.0));
	// The fine layer fades with distance on its own — its wavelength drops
	// under a pixel long before the coarse layer's does, so fading it early
	// kills the aliasing shimmer without flattening the whole far field.
	float fineFade = 1.0 - smoothstep(15.0, 90.0, viewDist);
	// With the baked cascade carrying the real spectrum detail, the procedural
	// ripple layer steps back to a fine close-up shimmer only.
	float cascadeOn = u_waterPlanarParams.z;
	vec3 noise = normalize(noiseCoarse * mix(1.0, 0.45, cascadeOn)
		+ noiseFine * (0.45 * fineFade));
	vec3 rippleNormal = normalize(noise.xzy * vec3(1.5, 1.0, 1.5));
	vec3 waveNormal = WaveNormal(v_wpos.xz);
	vec3 cascadeDetail = vec3(0.0, 0.0, 0.0);
	if (cascadeOn > 0.5) {
		cascadeDetail = CascadeDetail(v_wpos.xz);
		// The baked slope carries every band the texels can represent, from
		// ground swell to capillary chop, so it perturbs the analytic normal
		// the way the removed procedural octaves used to — but with content
		// that matches the displaced field instead of unrelated hash noise.
		waveNormal = normalize(vec3(waveNormal.x - cascadeDetail.x * 0.8, 1.0,
		                            waveNormal.z - cascadeDetail.y * 0.8));
	}
	// Distant water still calms toward the plane normal so the horizon mirrors
	// the sky, but keeps enough chop that its specular stays broken up instead
	// of collapsing into a clean geometric sun streak.
	float flatten = smoothstep(40.0, 320.0, viewDist);
	vec3 surfaceNormal = normalize(waveNormal + vec3(rippleNormal.x, 0.0, rippleNormal.z) * 0.55);
	surfaceNormal = normalize(mix(surfaceNormal, vec3(0.0, 1.0, 0.0), flatten * 0.7));

	vec3 diffuseLight = vec3(0.0);
	vec3 specularLight = vec3(0.0);
	vec3 worldToEye = u_waterCamera.xyz - v_wpos.xyz;
	vec3 eyeDirection = normalize(worldToEye);
	sunLight(surfaceNormal, eyeDirection, 240.0, 1.0, 0.12, diffuseLight, specularLight);
	// The glint control scales the sun's specular streak; without it a low sun
	// paints the whole mid-distance in its colour instead of a sun path.
	specularLight *= clamp(u_waterAdvanced.z, 0.0, 2.0) * clamp(u_waterSunDir.w, 0.0, 1.5);
	// Glitter break-up: real sun paths are a shimmer of discrete facets, not a
	// continuous polished stripe. A fast independent noise field modulates the
	// specular so the streak sparkles and never repeats along the wave rows.
	float sparkle = WaterGradNoise(v_wpos.xz * size * 26.0
		+ vec2(u_waterSurface.x * 1.7, -u_waterSurface.x * 1.3));
	specularLight *= 0.55 + 0.9 * smoothstep(-0.25, 0.55, sparkle);

	float distance = length(worldToEye);
	float distScale = clamp(u_waterOptics.y, 0.05, 1.5);
	// The full shaded normal drives the refraction offset so both the swell
	// and the ripples visibly bend the underwater image (safe now that the
	// screen UV is perspective-correct).
	vec2 distortion = surfaceNormal.xz * (0.008 + 0.05 / max(distance, 1.0)) * distScale;
	// Perspective-correct screen UV from the interpolated clip position.
	vec2 ndc = v_cascade.xy / max(v_cascade.w, 1e-4);
	vec2 screenUv = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
	vec3 refractedScene = vec3(0.0);
	float sceneDepth = 1.0;
	float waterDepth = LinearizeDepth(gl_FragCoord.z);
	float waterColumn = u_waterDeep.w;
	float shoreMask = 0.0;
	if (u_waterDepthParams.w > 0.5) {
		// The water column comes from the undistorted screen position: the
		// distorted lookup can land on foreground geometry (the island's own
		// silhouette), which read as a white band that swam with the camera.
		// A cross-filtered depth read softens the one-pixel silhouette jump at
		// underwater geometry edges, which otherwise cuts a hard jagged border
		// between the shallow and deep water colours.
		vec2 dTexel = u_viewTexel.xy * 1.5;
		float bedDepth = (LinearizeDepth(texture2D(s_waterSceneDepth, screenUv).x)
			+ LinearizeDepth(texture2D(s_waterSceneDepth, screenUv + vec2(dTexel.x, 0.0)).x)
			+ LinearizeDepth(texture2D(s_waterSceneDepth, screenUv - vec2(dTexel.x, 0.0)).x)
			+ LinearizeDepth(texture2D(s_waterSceneDepth, screenUv + vec2(0.0, dTexel.y)).x)
			+ LinearizeDepth(texture2D(s_waterSceneDepth, screenUv - vec2(0.0, dTexel.y)).x)) * 0.2;
		waterColumn = clamp(bedDepth - waterDepth, 0.0, max(u_waterDeep.w, 0.001));
		// Lens-style refraction: bend the view ray at the surface (Snell, eta
		// air->water) and let the screen offset grow with the water column, so a
		// deep column bends more — like looking through a convex lens — while
		// the ripple layer only adds shimmer on top.
		vec3 refrDir = refract(-eyeDirection, surfaceNormal, 0.75);
		float lensScale = clamp(waterColumn, 0.0, 3.0) / max(distance, 2.0) * 2.0;
		// The offset dies out toward the screen border: a bent sample outside
		// the copy would clamp-stretch the edge pixels into smeared streaks.
		vec2 edge = min(screenUv, vec2_splat(1.0) - screenUv);
		float edgeFade = smoothstep(0.0, 0.12, min(edge.x, edge.y));
		vec2 refractUv = clamp(screenUv
			+ (refrDir.xz * lensScale * 0.35 + distortion) * u_waterCamera.w * edgeFade,
			vec2_splat(0.001), vec2_splat(0.999));
		// Reject the bent colour sample when it lands in front of the surface
		// (foreground leak), falling back to the straight-through view.
		float refrDepth = LinearizeDepth(texture2D(s_waterSceneDepth, refractUv).x);
		vec2 sceneUv = refrDepth < waterDepth - 0.05 ? screenUv : refractUv;
		// Small box filter over the scene copy: the copy carries no MSAA, so a
		// single fetch shows every geometry step edge through the water.
		vec2 texel = u_viewTexel.xy;
		refractedScene = (texture2D(s_waterScene, sceneUv + vec2(-0.75, -0.25) * texel).xyz
		                + texture2D(s_waterScene, sceneUv + vec2(0.25, -0.75) * texel).xyz
		                + texture2D(s_waterScene, sceneUv + vec2(0.75, 0.25) * texel).xyz
		                + texture2D(s_waterScene, sceneUv + vec2(-0.25, 0.75) * texel).xyz) * 0.25;
		float shoreWidth = max(u_waterOptics.y * 0.5, 0.05);
		shoreMask = 1.0 - smoothstep(0.0, shoreWidth, waterColumn);
	}

	// Planar reflection lookup via the mirrored camera this engine already runs.
	vec3 reflectionSample = vec3(0.0);
	if (u_waterPlanarParams.x > 0.5) {
		vec4 clip = mul(u_waterPlanarViewProj, vec4(v_wpos.xyz, 1.0));
		if (clip.w > 1e-4) {
			vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
			if (u_waterPlanarParams.y > 0.5) {
				uv.y = 1.0 - uv.y;
			}
			uv = clamp(uv + distortion, vec2_splat(0.001), vec2_splat(0.999));
			reflectionSample = texture2D(s_waterPlanar, uv).xyz;
		}
	} else {
		// Fallback: analytic sky reflection when no planar RT is bound.
		vec3 reflected = reflect(-eyeDirection, surfaceNormal);
		float upward = clamp(reflected.y * 0.5 + 0.5, 0.0, 1.0);
		reflectionSample = mix(u_waterSkyHorizon.xyz, u_waterSkyZenith.xyz,
		                       upward * upward) * u_waterSkyZenith.w;
	}

	// Schlick Fresnel (water F0 ~ 0.02), verbatim from three.js Water.js.
	float theta = max(dot(eyeDirection, surfaceNormal), 0.0);
	float rf0 = 0.02;
	// Grazing reflectance capped below 1 so the water keeps some of its own
	// colour near the horizon; high enough that shallow viewing angles stop
	// showing the underwater scene the way real water does.
	float reflectance = rf0 + (0.82 - rf0) * pow(1.0 - theta, 5.0);

	// Exponential shallow-to-deep transition: asymptotic like real water, with
	// no hard edge where the column depth saturates.
	float depthMix = 1.0 - exp(-waterColumn * 3.0 / max(u_waterDeep.w, 1e-3));
	vec3 bodyColor = mix(u_waterShallow.xyz, u_waterDeep.xyz, depthMix);
	vec3 ambientFill = u_waterAmbient.xyz * u_waterAmbient.w;
	vec3 scatteringTint = UnpackScatteringTint(u_waterAdvanced.w);
	float absorption = max(u_waterShallow.w, 0.0);
	// Beer-Lambert with the body colour as the surviving spectrum: light that
	// is not transmitted through the column is replaced by in-scattered body
	// colour, so deep water settles on the authored blue instead of black.
	vec3 transmission = exp(-(vec3_splat(1.0) - bodyColor) * absorption * waterColumn * 2.0);
	vec3 scatter = bodyColor * (0.5 + 0.5 * max(dot(surfaceNormal, eyeDirection), 0.0))
		+ ambientFill;
	float backLight = pow(clamp(1.0 - max(dot(-normalize(u_waterSunDir.xyz), surfaceNormal), 0.0), 0.0, 1.0), 2.0);
	scatter += scatteringTint * u_waterAdvanced.x * backLight * (1.0 - depthMix);
	vec3 refracted = mix(bodyColor, refractedScene, clamp(u_waterCamera.w, 0.0, 1.0));
	vec3 baseWater = mix(scatter, refracted, transmission);
	vec3 foamColor = mix(baseWater, vec3_splat(0.9), 0.75);
	// fBm foam mask: a single value-noise octave is a grid of square blobs; the
	// rotated-octave fBm breaks the patches into irregular lapping shapes.
	float foamNoise = clamp(WaterHeightFbm(v_wpos.xz
		* (0.35 * max(u_waterAdvanced.y, 0.1))) * 0.75 + 0.5, 0.0, 1.0);
	// Whitecaps live on the crests the vertex stage flagged, broken up by noise.
	float crestFoam = smoothstep(0.72, 0.95, v_wave.w) * smoothstep(0.35, 0.75, foamNoise);
	// The cascade's fold channel marks where the spectrum actually compresses
	// the surface toward breaking, which puts whitecaps on real crest lines
	// instead of wherever the noise happens to be bright.
	float cascadeFoam = smoothstep(0.55, 1.1, cascadeDetail.z)
		* smoothstep(0.3, 0.7, foamNoise) * cascadeOn;
	crestFoam = max(crestFoam, cascadeFoam);
	// Shore foam is a lapping band, not a solid wall: noise breaks it up and it
	// stays subtle so the beach reads wet rather than painted white.
	float foam = shoreMask * u_waterOptics.z * foamNoise * 0.5;
	foam = clamp(max(foam, crestFoam * u_waterOptics.z * 0.6), 0.0, 1.0);

	// Composite: three.js mixes (sun*diffuse*0.3 + scatter)*shadowMask with
	// reflection + specular by reflectance. shadowMask = 1.0 here.
	vec3 albedo = mix((u_waterSunColor.xyz * diffuseLight * 0.3 + baseWater),
	                  reflectionSample + specularLight, reflectance);
	albedo = mix(albedo, foamColor, foam);

	// Aerial perspective: far water converges on the analytic sky reflection,
	// so the surface meets the sky softly instead of on a hard painted edge.
	// Driven by the grazing angle rather than raw distance, so the blend
	// tracks where the surface visually meets the sky at any camera height.
	float grazing = 1.0 - theta;
	float horizonFade = smoothstep(0.82, 0.995, grazing) * smoothstep(30.0, 150.0, distance);
	vec3 skyAtHorizon = mix(u_waterSkyHorizon.xyz, u_waterSkyZenith.xyz, 0.15)
		* clamp(u_waterSkyZenith.w, 0.0, 1.2);
	albedo = mix(albedo, skyAtHorizon, horizonFade * 0.9);

	// Opaque: the surface is a solid plane, not a transparent layer.
	gl_FragColor = vec4(albedo, 1.0);
}
