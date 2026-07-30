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
uniform vec4 u_waterPlanarParams; // x = planar bound, y = flip V
uniform mat4 u_waterPlanarViewProj;
uniform vec4 u_waterSurface;      // x = clock, y = wave count, z = animated, w = plane height
uniform vec4 u_waterWaveA[4];     // per wave: dirX, dirZ, amplitude, wavelength
uniform vec4 u_waterWaveB[4];     // per wave: speed, steepness (normalised Q), 0, 0

SAMPLER2D(s_waterScene, 0);
SAMPLER2D(s_waterSceneDepth, 1);
SAMPLER2D(s_waterPlanar, 2);
SAMPLER2D(s_waterCascade, 3); // unused

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

float WaterValueNoise(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	return mix(mix(WaterHash(i),                  WaterHash(i + vec2(1.0, 0.0)), f.x),
	           mix(WaterHash(i + vec2(0.0, 1.0)), WaterHash(i + vec2(1.0, 1.0)), f.x), f.y);
}

vec3 getNoise(vec2 uv)
{
	float time = u_waterSurface.x;
	// Each octave samples a rotated copy of the plane so the value-noise cell
	// lattice of the four octaves never lines up — axis-aligned squares would
	// otherwise show through the sum.
	vec2 r1 = vec2(uv.x * 0.8 - uv.y * 0.6, uv.x * 0.6 + uv.y * 0.8);
	vec2 r2 = vec2(uv.x * 0.30902 - uv.y * 0.95106, uv.x * 0.95106 + uv.y * 0.30902);
	vec2 r3 = vec2(-uv.x * 0.5 - uv.y * 0.86603, uv.x * 0.86603 - uv.y * 0.5);
	// Same scroll frequencies and time scales as three.js Water.js getNoise.
	vec2 uv0 = (uv / 103.0) + vec2(time / 17.0, time / 29.0);
	vec2 uv1 = r1 / 107.0 - vec2(time / -19.0, time / 31.0);
	vec2 uv2 = r2 / vec2(897.0, 983.0) + vec2(time / 101.0, time / 97.0);
	vec2 uv3 = r3 / vec2(191.0, 127.0) - vec2(time / 109.0, time / -113.0);
	// Four octaves of value noise summed; the original summed four normal-map
	// samples so the magnitude is comparable. Each octave in [0,1], sum in
	// [0,4], map to [-1,1] like the original's `noise * 0.5 - 1.0`.
	float n0 = WaterValueNoise(uv0);
	float n1 = WaterValueNoise(uv1);
	float n2 = WaterValueNoise(uv2);
	float n3 = WaterValueNoise(uv3);
	float sum = (n0 + n1 + n2 + n3) * 0.5 - 1.0; // [-1,1]
	// Build a tangent-space normal: the noise drives horizontal slope, z is the
	// reconstructed up component so the vector stays near-unit and points up.
	float nx = sum * 0.45;
	float nz = sum * 0.45;
	float ny = sqrt(max(1.0 - nx * nx - nz * nz, 0.0));
	return vec3(nx, ny, nz);
}

// Analytic Gerstner normal evaluated per fragment, so the swell shading is
// smooth across the grid's quads instead of faceting on vertex interpolation.
vec3 WaveNormal(vec2 pos)
{
	float time = u_waterSurface.x;
	int waveCount = int(min(u_waterSurface.y, 4.0));
	float nx = 0.0;
	float nz = 0.0;
	if (u_waterSurface.z > 0.5) {
		for (int i = 0; i < 4; ++i) {
			if (i >= waveCount) { break; }
			vec4 wa = u_waterWaveA[i];
			vec4 wb = u_waterWaveB[i];
			float k = 6.2831853 / max(wa.w, 1e-3);
			vec2 dir = normalize(wa.xy + vec2(1e-5, 0.0));
			float c = cos(k * (dot(dir, pos) - wb.x * time));
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
	vec3 noiseCoarse = getNoise(v_wpos.xz * size * 300.0);
	vec3 noiseFine = getNoise(v_wpos.xz * size * 1400.0 + vec2(37.0, 91.0));
	vec3 noise = normalize(noiseCoarse + noiseFine * 0.4);
	vec3 rippleNormal = normalize(noise.xzy * vec3(1.5, 1.0, 1.5));
	vec3 waveNormal = WaveNormal(v_wpos.xz);
	vec3 surfaceNormal = normalize(waveNormal + vec3(rippleNormal.x, 0.0, rippleNormal.z) * 0.55);

	vec3 diffuseLight = vec3(0.0);
	vec3 specularLight = vec3(0.0);
	vec3 worldToEye = u_waterCamera.xyz - v_wpos.xyz;
	vec3 eyeDirection = normalize(worldToEye);
	sunLight(surfaceNormal, eyeDirection, 240.0, 1.0, 0.12, diffuseLight, specularLight);
	// The glint control scales the sun's specular streak; without it a low sun
	// paints the whole mid-distance in its colour instead of a sun path.
	specularLight *= clamp(u_waterAdvanced.z, 0.0, 2.0) * clamp(u_waterSunDir.w, 0.0, 1.5);

	float distance = length(worldToEye);
	float distScale = clamp(u_waterOptics.y, 0.05, 1.5);
	// Only the fine ripple layer feeds the refraction offset: the Gerstner
	// normal has kilometre-long straight wavefronts which read as long jagged
	// polylines in the refracted image instead of a shimmering surface.
	vec2 distortion = rippleNormal.xz * (0.0006 + 0.02 / max(distance, 1.0)) * distScale;
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
		float lensScale = clamp(waterColumn, 0.0, 3.0) / max(distance, 2.0);
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
	float foamNoise = WaterValueNoise(v_wpos.xz * (0.35 * max(u_waterAdvanced.y, 0.1)) + vec2(u_waterSurface.x * 0.05, u_waterSurface.x * 0.05));
	// Whitecaps live on the crests the vertex stage flagged, broken up by noise.
	float crestFoam = smoothstep(0.72, 0.95, v_wave.w) * smoothstep(0.35, 0.75, foamNoise);
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
	float farPlane = max(u_waterDepthParams.y, 1.0);
	float horizonFade = smoothstep(farPlane * 0.55, farPlane * 0.98, distance);
	vec3 skyAtHorizon = mix(u_waterSkyHorizon.xyz, u_waterSkyZenith.xyz, 0.15)
		* clamp(u_waterSkyZenith.w, 0.0, 1.2);
	albedo = mix(albedo, skyAtHorizon, horizonFade * 0.85);

	// Opaque: the surface is a solid plane, not a transparent layer.
	gl_FragColor = vec4(albedo, 1.0);
}
