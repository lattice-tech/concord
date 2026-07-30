$input v_wpos, v_uv, v_surface, v_cascade

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
uniform vec4 u_waterSurface;      // x = clock, z = animated, w = plane height

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
	// Same scroll frequencies and time scales as three.js Water.js getNoise.
	vec2 uv0 = (uv / 103.0) + vec2(time / 17.0, time / 29.0);
	vec2 uv1 = uv / 107.0 - vec2(time / -19.0, time / 31.0);
	vec2 uv2 = uv / vec2(8907.0, 9803.0) + vec2(time / 101.0, time / 97.0);
	vec2 uv3 = uv / vec2(1091.0, 1027.0) - vec2(time / 109.0, time / -113.0);
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
	float nx = sum * 0.3;
	float nz = sum * 0.3;
	float ny = sqrt(max(1.0 - nx * nx - nz * nz, 0.0));
	return vec3(nx, ny, nz);
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
	// Our getNoise returns a vec3 tangent normal (x,y,z) = (slope, up, slope).
	// noise.xzy swaps to (slope, slope, up), then scales — same perturbation.
	vec3 noise = getNoise(v_uv * size);
	vec3 surfaceNormal = normalize(noise.xzy * vec3(1.5, 1.0, 1.5));

	vec3 diffuseLight = vec3(0.0);
	vec3 specularLight = vec3(0.0);
	vec3 worldToEye = u_waterCamera.xyz - v_wpos.xyz;
	vec3 eyeDirection = normalize(worldToEye);
	sunLight(surfaceNormal, eyeDirection, 100.0, 2.0, 0.5, diffuseLight, specularLight);

	float distance = length(worldToEye);
	float distScale = clamp(u_waterOptics.y, 0.05, 1.5);
	vec2 distortion = surfaceNormal.xz * (0.0006 + 0.02 / max(distance, 1.0)) * distScale;
	vec2 screenUv = vec2(v_cascade.x, 1.0 - v_cascade.y);
	vec2 refractUv = clamp(screenUv + distortion * u_waterCamera.w,
	                      vec2_splat(0.001), vec2_splat(0.999));
	vec3 refractedScene = vec3(0.0);
	float sceneDepth = 1.0;
	float waterDepth = LinearizeDepth(gl_FragCoord.z);
	float waterColumn = u_waterDeep.w;
	float shoreMask = 0.0;
	if (u_waterDepthParams.w > 0.5) {
		refractedScene = texture2D(s_waterScene, refractUv).xyz;
		sceneDepth = texture2D(s_waterSceneDepth, refractUv).x;
		float bedDepth = LinearizeDepth(sceneDepth);
		waterColumn = clamp(bedDepth - waterDepth, 0.0, max(u_waterDeep.w, 0.001));
		float shoreWidth = max(u_waterOptics.y * 0.15, 0.05);
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
	float reflectance = rf0 + (1.0 - rf0) * pow(1.0 - theta, 5.0);

	float depthMix = clamp(waterColumn / max(u_waterDeep.w, 1e-3), 0.0, 1.0);
	vec3 bodyColor = mix(u_waterShallow.xyz, u_waterDeep.xyz, depthMix);
	vec3 ambientFill = u_waterAmbient.xyz * u_waterAmbient.w;
	vec3 scatteringTint = UnpackScatteringTint(u_waterAdvanced.w);
	float absorption = max(u_waterShallow.w, 0.0);
	vec3 transmission = exp(-bodyColor * absorption * waterColumn * 1.5);
	vec3 shallowScene = refractedScene * transmission;
	vec3 scatter = bodyColor * (0.35 + max(dot(surfaceNormal, eyeDirection), 0.0))
		+ ambientFill;
	float backLight = pow(clamp(1.0 - max(dot(-normalize(u_waterSunDir.xyz), surfaceNormal), 0.0), 0.0, 1.0), 2.0);
	scatter += scatteringTint * u_waterAdvanced.x * backLight * (1.0 - depthMix);
	vec3 baseWater = mix(scatter, shallowScene + scatter * 0.35, clamp(u_waterCamera.w, 0.0, 1.0));
	vec3 foamColor = mix(vec3_splat(1.0), scatteringTint + vec3_splat(0.35), 0.35);
	float foamNoise = WaterValueNoise(v_uv * (3.0 * max(u_waterAdvanced.y, 0.1)) + vec2(u_waterSurface.x * 0.05));
	float crestFoam = smoothstep(0.58, 0.82, foamNoise + (1.0 - theta) * 0.35);
	float foam = shoreMask * u_waterOptics.z;
	foam = clamp(max(foam, crestFoam * u_waterOptics.z * 0.35), 0.0, 1.0);

	// Composite: three.js mixes (sun*diffuse*0.3 + scatter)*shadowMask with
	// reflection + specular by reflectance. shadowMask = 1.0 here.
	vec3 albedo = mix((u_waterSunColor.xyz * diffuseLight * 0.3 + baseWater),
	                  reflectionSample + specularLight, reflectance);
	albedo = mix(albedo, foamColor, foam);

	// Opaque: the surface is a solid plane, not a transparent layer.
	gl_FragColor = vec4(albedo, 1.0);
}
