$input v_ray, v_uv

#include <bgfx_shader.sh>

// Local volumetric smoke pass (see BgfxSmokeRenderer). Each authored
// SmokeVolume bounds a region of participating medium. The density is an
// animated fractal field sampled from a precomputed **3D noise texture**
// (hardware trilinear filtering) rather than evaluated with per-step ALU noise,
// which is both far cheaper on the GPU and smoother. The field is carved to a
// coverage threshold, eroded by a higher-frequency tap, and faded toward the
// boundary (box faces or an inscribed ellipsoid), and it is scrolled by an
// accumulated wind + buoyancy offset so the smoke drifts and roils. The field
// is ray-marched with per-step Beer-Lambert extinction and a one-tap light
// march toward the sun for single scattering (Henyey-Greenstein phase). Scene
// depth truncates each ray at the nearest opaque surface. Output is
// premultiplied-alpha HDR, composited over the scene color with
// (ONE, INV_SRC_ALPHA).

#define MAX_SMOKE 8

SAMPLER2D(s_smSceneDepth, 0);
SAMPLER3D(s_smNoise, 1);

uniform mat4 u_smInvViewProj;
// xyz: camera world position, w: near-plane NDC z.
uniform vec4 u_smCamera;
// x: active volume count, y: sun influence [0,1], z: march steps, w: unused.
uniform vec4 u_smParams;
// xyz: sun travel direction (light points this way), w: unused.
uniform vec4 u_smSunDir;
// Per-volume: xyz box min (world), w density scale.
uniform vec4 u_smBoxMin[MAX_SMOKE];
// Per-volume: xyz box max (world), w noise cell size.
uniform vec4 u_smBoxMax[MAX_SMOKE];
// Per-volume: rgb color (sRGB), w coverage.
uniform vec4 u_smColor[MAX_SMOKE];
// Per-volume: x edge softness, y detail, z anisotropy, w shape (0 box, 1 ellipsoid).
uniform vec4 u_smShape[MAX_SMOKE];
// Per-volume: xyz accumulated field offset (wind + buoyancy), w emissive.
uniform vec4 u_smWind[MAX_SMOKE];

vec3 SmokeToLinear(vec3 color)
{
	return pow(max(color, vec3_splat(0.0)), vec3_splat(2.2));
}

float HenyeyGreenstein(float cosAngle, float g)
{
	float gg = g * g;
	return (1.0 - gg) / pow(max(1.0 + gg - 2.0 * g * cosAngle, 1e-4), 1.5);
}

// Fractional smoke density at a world point for one volume: an FBM field (baked
// into the 3D noise texture, R = broad shape, G = fine detail) carved to
// coverage and eroded, multiplied by a soft boundary falloff so the medium
// dissolves toward the box faces / ellipsoid shell. Two texture fetches replace
// the ~32 ALU ops the previous inline value-noise FBM cost per sample.
float SampleDensity(vec3 worldPos, vec3 boxMin, vec3 boxMax, vec3 windOffset,
	float noiseScale, float coverage, float detail, float edge, float shapeFlag,
	float densityScale)
{
	vec3 center = (boxMin + boxMax) * 0.5;
	vec3 half = max((boxMax - boxMin) * 0.5, vec3_splat(1e-3));
	vec3 local = (worldPos - center) / half;

	float falloff;
	if (shapeFlag > 0.5)
	{
		float r = length(local);
		falloff = 1.0 - smoothstep(1.0 - edge, 1.0, r);
	}
	else
	{
		vec3 a = abs(local);
		float fx = 1.0 - smoothstep(1.0 - edge, 1.0, a.x);
		float fy = 1.0 - smoothstep(1.0 - edge, 1.0, a.y);
		float fz = 1.0 - smoothstep(1.0 - edge, 1.0, a.z);
		falloff = fx * fy * fz;
	}
	if (falloff <= 0.0)
	{
		return 0.0;
	}

	// Three decorrelated octaves of the tileable noise give billowing structure
	// (a single low octave alone reads as one smooth "ball"). Scales stay under
	// a couple of tiles across the box so repetition is not obvious, and the
	// octaves combine into one connected turbulence field — not thresholded into
	// separate islands (which read as discrete puffs).
	vec3 samplePoint = (worldPos - windOffset) / max(noiseScale, 0.01);
	float oct0 = texture3D(s_smNoise, samplePoint * 0.9).r;
	float oct1 = texture3D(s_smNoise, samplePoint * 2.3 + vec3(0.37, 0.11, 0.53)).g;
	float oct2 = texture3D(s_smNoise, samplePoint * 4.7 + vec3(0.19, 0.63, 0.27)).r;
	float turbulence = oct0 * 0.55 + oct1 * 0.30 + oct2 * 0.15;
	// Shape the turbulence into wispy billows (not a solid ball): coverage widens
	// the low end. The boundary falloff — not a hard noise threshold — dissolves
	// the edges, and the smooth field keeps the medium connected.
	float lowEdge = 0.34 - clamp(coverage, 0.0, 1.0) * 0.26;
	float shaped = smoothstep(lowEdge, 0.92, turbulence);

	// Faint drifting wisps ("若隐若现"): a fine, differently-scrolled octave
	// modulates only the low-density fringe, so thin tendrils fade in and out
	// as the field moves instead of the edge being a uniform soft shell.
	float wisp = texture3D(s_smNoise, samplePoint * 3.3 + vec3(0.7, 0.2, 0.5)).g;
	float fringe = 1.0 - smoothstep(0.0, 0.55, shaped);
	shaped *= mix(1.0, 0.3 + 1.4 * wisp, fringe);

	float detailErode = 1.0 - (1.0 - oct2) * clamp(detail, 0.0, 1.0) * 0.3;
	return clamp(shaped, 0.0, 1.0) * detailErode * falloff * densityScale;
}

void main()
{
	vec3 rayOrigin = u_smCamera.xyz;
	vec3 rayDir = normalize(v_ray);

	// Scene depth -> world distance along the ray for occlusion truncation
	// (offscreen RTs are top-left origin on Vulkan, so flip V on the fetch).
	float sceneDistance = 1e9;
	vec2 depthUv = vec2(v_uv.x, 1.0 - v_uv.y);
	float depth = texture2D(s_smSceneDepth, depthUv).x;
	if (depth < 1.0)
	{
		vec2 ndc = v_uv * 2.0 - 1.0;
		vec4 worldPosition = mul(u_smInvViewProj, vec4(ndc, depth, 1.0));
		worldPosition.xyz /= worldPosition.w;
		sceneDistance = length(worldPosition.xyz - rayOrigin);
	}
	// Device depth passed straight through for the composite pass's
	// depth-aware upsample (second attachment, MRT). Not remapped: the
	// composite compares it against the same full-res depth texture, so only
	// consistency matters, not a particular unit.
	float depthProxy = depth;

	vec3 toSun = normalize(-u_smSunDir.xyz);
	vec3 invDir = vec3_splat(1.0) / rayDir;
	float cosAngle = dot(rayDir, toSun);
	float sunInfluence = clamp(u_smParams.y, 0.0, 1.0);
	int steps = int(u_smParams.z);
	// Per-pixel jitter breaks fixed-step banding into fine noise.
	float jitter = fract(sin(dot(v_uv, vec2(12.9898, 78.233))) * 43758.5453);

	vec3 accumColor = vec3_splat(0.0);
	float accumAlpha = 0.0;
	int count = int(u_smParams.x);
	for (int i = 0; i < MAX_SMOKE; ++i)
	{
		if (i >= count)
		{
			break;
		}
		vec3 boxMin = u_smBoxMin[i].xyz;
		vec3 boxMax = u_smBoxMax[i].xyz;
		float densityScale = u_smBoxMin[i].w;
		float noiseScale = u_smBoxMax[i].w;
		vec3 colorLinear = SmokeToLinear(u_smColor[i].rgb);
		float coverage = u_smColor[i].w;
		float edge = u_smShape[i].x;
		float detail = u_smShape[i].y;
		float anisotropy = u_smShape[i].z;
		float shapeFlag = u_smShape[i].w;
		vec3 windOffset = u_smWind[i].xyz;
		float emissive = u_smWind[i].w;

		// Ray/AABB slab intersection.
		vec3 tNear = (boxMin - rayOrigin) * invDir;
		vec3 tFar = (boxMax - rayOrigin) * invDir;
		vec3 tSmaller = min(tNear, tFar);
		vec3 tBigger = max(tNear, tFar);
		float tMin = max(max(tSmaller.x, tSmaller.y), tSmaller.z);
		float tMax = min(min(tBigger.x, tBigger.y), tBigger.z);
		float enter = max(tMin, 0.0);
		float exitT = min(tMax, sceneDistance);
		if (tMax <= enter || exitT <= enter)
		{
			continue;
		}

		float stepSize = (exitT - enter) / float(steps);
		float phase = mix(HenyeyGreenstein(cosAngle, anisotropy),
			HenyeyGreenstein(cosAngle, -0.12 * anisotropy), 0.35);
		float lightStep = max(noiseScale, 0.25);

		float transmittance = 1.0;
		vec3 radiance = vec3_splat(0.0);
		for (int step = 0; step < 128; ++step)
		{
			if (step >= steps)
			{
				break;
			}
			float t = enter + (float(step) + jitter) * stepSize;
			if (t >= exitT)
			{
				break;
			}
			vec3 samplePos = rayOrigin + rayDir * t;
			float density = SampleDensity(samplePos, boxMin, boxMax, windOffset,
				noiseScale, coverage, detail, edge, shapeFlag, densityScale);
			if (density > 0.002)
			{
				float lightDensity = SampleDensity(samplePos + toSun * lightStep,
					boxMin, boxMax, windOffset, noiseScale, coverage, detail, edge,
					shapeFlag, densityScale);
				// Softer self-shadow with a high ambient floor so the thick
				// interior stays a lit grey instead of collapsing to black. A
				// gentle top-down sky fill lifts the shaded side so it reads as
				// dim rather than dark.
				float lightVisibility = exp(-lightDensity * 1.0);
				float forward = clamp(phase, 0.0, 4.0) * 0.15 * sunInfluence;
				float skyFill = 0.12 * (0.5 + 0.5 * rayDir.y);
				float lightTerm = clamp(0.66 + skyFill + lightVisibility * (0.36 * sunInfluence + forward),
					0.0, 1.3);
				vec3 lit = colorLinear * lightTerm + colorLinear * emissive;

				float opticalDepth = density * stepSize * 2.0;
				float alpha = 1.0 - exp(-opticalDepth);
				radiance += lit * alpha * transmittance;
				transmittance *= 1.0 - alpha;
				if (transmittance < 0.02)
				{
					break;
				}
			}
		}

		float volAlpha = 1.0 - transmittance;
		accumColor += (1.0 - accumAlpha) * radiance;
		accumAlpha += (1.0 - accumAlpha) * volAlpha;
	}

#if SMOKE_MRT
	// Main path: second attachment carries device depth for the composite
	// pass's depth-aware upsample (see BgfxSmokeRenderer / ViewSlot.smokeFb).
	gl_FragData[0] = vec4(accumColor, accumAlpha);
	gl_FragData[1] = vec4(depthProxy, 0.0, 0.0, 0.0);
#else
	// Compose-only path (reflection cubemap face): single color attachment,
	// no depth proxy needed or available.
	gl_FragColor = vec4(accumColor, accumAlpha);
#endif
}
